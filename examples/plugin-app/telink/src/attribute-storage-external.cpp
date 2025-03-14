/**
 *
 *    Copyright (c) 2020-2023 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <app/util/attribute-storage.h>

#include <app/util/attribute-storage-detail.h>

#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/CommandHandlerInterfaceRegistry.h>
#include <app/InteractionModelEngine.h>
#include <app/reporting/reporting.h>
#include <app/util/config.h>
#include <app/util/ember-strings.h>
#include <app/util/endpoint-config-api.h>
#include <app/util/generic-callbacks.h>
#include <app/util/persistence/AttributePersistenceProvider.h>
#include <lib/core/CHIPConfig.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/logging/CHIPLogging.h>
#include <platform/LockTracker.h>
#include <protocols/interaction_model/StatusCode.h>

#include "attribute-storage-external.h"
#include "attribute-storage-detail-external.h"

using chip::Protocols::InteractionModel::Status;

// Attribute storage depends on knowing the current layout/setup of attributes
// and corresponding callbacks. Specifically:
//   - zap-generated/callback.h is needed because endpoint_config will call the
//     corresponding callbacks (via GENERATED_FUNCTION_ARRAYS) and the include
//     for it is:
//     util/config.h -> zap-generated/endpoint_config.h
#include <app-common/zap-generated/callback.h>

using namespace chip;
using namespace chip::app;

extern EmberAfDefinedEndpoint emAfEndpoints[MAX_ENDPOINT_COUNT];

static void initializeEndpoint(EmberAfDefinedEndpoint * definedEndpoint)
{
    uint8_t clusterIndex;
    const EmberAfEndpointType * epType = definedEndpoint->endpointType;
    for (clusterIndex = 0; clusterIndex < epType->clusterCount; clusterIndex++)
    {
        const EmberAfCluster * cluster = &(epType->cluster[clusterIndex]);
        EmberAfGenericClusterFunction f;
        emberAfClusterInitCallback(definedEndpoint->endpoint, cluster->clusterId);
        f = emberAfFindClusterFunction(cluster, CLUSTER_MASK_INIT_FUNCTION);
        if (f != nullptr)
        {
            ((EmberAfInitFunction) f)(definedEndpoint->endpoint);
        }
    }
}

static void emAfLoadAttributeDefaultsExternal(EndpointId endpoint, Optional<ClusterId> = NullOptional);

void emAfCallInitsExternal(chip::EndpointId endpointId)
{
    initializeEndpoint(&(emAfEndpoints[endpointId]));
}

void emberAfInitializeAttributesExternal(EndpointId endpoint)
{
    emAfLoadAttributeDefaultsExternal(endpoint);
}

void emAfLoadAttributeDefaultsExternal(EndpointId endpoint, Optional<ClusterId> clusterId)
{
    uint16_t ep;
    uint8_t clusterI;
    uint16_t attr;
    uint8_t * ptr;
    uint16_t epCount = emberAfEndpointCount();
    uint8_t attrData[ATTRIBUTE_LARGEST];
    auto * attrStorage = GetAttributePersistenceProvider();

    for (ep = 0; ep < epCount; ep++)
    {
        EmberAfDefinedEndpoint * de;
        if (endpoint != kInvalidEndpointId)
        {
            ep = emberAfIndexFromEndpoint(endpoint);
            if (ep == kEmberInvalidEndpointIndex)
            {
                return;
            }
        }
        de = &(emAfEndpoints[ep]);

        for (clusterI = 0; clusterI < de->endpointType->clusterCount; clusterI++)
        {
            const EmberAfCluster * cluster = &(de->endpointType->cluster[clusterI]);

            if (clusterId.HasValue())
            {
                if (clusterId.Value() != cluster->clusterId)
                {
                    continue;
                }
            }

            if (cluster->attributeCount > 300)
            {
                // halResetWatchdog();
            }

            for (attr = 0; attr < cluster->attributeCount; attr++)
            {
                const EmberAfAttributeMetadata * am = &(cluster->attributes[attr]);
                ptr                                 = nullptr; // Will get set to the value to write, as needed.

                // First check for a persisted value.
                if ((am->mask & ATTRIBUTE_MASK_NONVOLATILE) && (am->mask & ATTRIBUTE_MASK_EXTERNAL_STORAGE))
                {
                    EmberAfAttributeSearchRecord record;
                    record.endpoint    = de->endpoint;
                    record.clusterId   = cluster->clusterId;
                    record.attributeId = am->attributeId;
                    VerifyOrDieWithMsg(attrStorage != nullptr, Zcl, "Attribute persistence needs a persistence provider");
                    MutableByteSpan bytes(attrData);
                    CHIP_ERROR err =
                        attrStorage->ReadValue(ConcreteAttributePath(de->endpoint, cluster->clusterId, am->attributeId), am, bytes);
                    if (err == CHIP_NO_ERROR)
                    {
                        ptr = attrData;

                        emAfReadOrWriteAttribute(&record,
                                                nullptr, // metadata - unused
                                                ptr,
                                                0,     // buffer size - unused
                                                true); // write?
                    }
                }
            }
        }
        if (endpoint != kInvalidEndpointId)
        {
            break;
        }
    }
}

void emAfSaveAttributeToStorageIfNeededExternal(uint8_t * data, EndpointId endpoint, ClusterId clusterId,
                                        const EmberAfAttributeMetadata * metadata)
{
    // Get out of here if this attribute isn't marked non-volatile.
    if (!(metadata->mask & ATTRIBUTE_MASK_NONVOLATILE) || !(metadata->mask & ATTRIBUTE_MASK_EXTERNAL_STORAGE))
    {
        return;
    }

    // TODO: Maybe we should have a separate constant for the size of the
    // largest non-volatile attribute?
    uint8_t allZeroData[ATTRIBUTE_LARGEST] = { 0 };
    if (data == nullptr)
    {
        data = allZeroData;
    }

    size_t dataSize;
    EmberAfAttributeType type = metadata->attributeType;
    if (emberAfIsStringAttributeType(type))
    {
        dataSize = emberAfStringLength(data) + 1;
    }
    else if (emberAfIsLongStringAttributeType(type))
    {
        dataSize = emberAfLongStringLength(data) + 2;
    }
    else
    {
        dataSize = metadata->size;
    }

    auto * attrStorage = GetAttributePersistenceProvider();
    if (attrStorage)
    {
        attrStorage->WriteValue(ConcreteAttributePath(endpoint, clusterId, metadata->attributeId), ByteSpan(data, dataSize));
        ChipLogProgress(DataManagement, "***********Store attribute value finished***********");
    }
    else
    {
        ChipLogProgress(DataManagement, "Can't store attribute value: no persistence provider");
    }
}