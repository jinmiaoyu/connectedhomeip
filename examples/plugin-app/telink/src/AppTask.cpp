/*
 *
 *    Copyright (c) 2022-2024 Project CHIP Authors
 *    All rights reserved.
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

#include "AppTask.h"
#include <app/server/Server.h>

#include "ColorFormat.h"
#include "LEDManager.h"
#include "PWMManager.h"

#include "Device.h"

#include <app-common/zap-generated/attributes/Accessors.h>
#include <app/util/af-types.h>
#include <app/util/attribute-storage.h>
#include <app/util/endpoint-config-api.h>
#include <app/clusters/identify-server/identify-server.h>
#include <app-common/zap-generated/callback.h>

#include "attribute-storage-external.h"

using namespace chip::app::Clusters;

LOG_MODULE_DECLARE(app, CONFIG_CHIP_APP_LOG_LEVEL);

#define DEVICE_TYPE_LO_ON_OFF_PLUG 0x010A

// Device Version for dynamic endpoints:
#define DEVICE_VERSION_DEFAULT 1

#define kFirstDynamicEndpointId 3

#define ZCL_ON_OFF_CLUSTER_REVISION (4u)
#define ZCL_ON_OFF_FEATURE_MAP (1u)
#define ZCL_IDENTIFY_CLUSTER_REVISION (4u)
#define ZCL_IDENTIFY_FEATURE_MAP (0u)
#define ZCL_GROUPS_CLUSTER_REVISION (4u)
#define ZCL_GROUPS_FEATURE_MAP (1u)
#define ZCL_LEVEL_CONTROL_CLUSTER_REVISION (6u)
#define ZCL_LEVEL_CONTROL_FEATURE_MAP (1u)
namespace {
bool sfixture_on;
uint8_t sBrightness;
AppTask::Fixture_Action sColorAction = AppTask::INVALID_ACTION;
XyColor_t sXY;
HsvColor_t sHSV;
CtColor_t sCT;
RgbColor_t sLedRgb;
} // namespace

AppTask AppTask::sAppTask;

namespace {
const int kNodeLabelSize = 32;
const int kUniqueIdSize  = 32;
// Current ZCL implementation of Struct uses a max-size array of 254 bytes
const int kDescriptorAttributeArraySize = 254;
const int kNameSupport = 128;

constexpr Identify::IdentifyTypeEnum kIdentifyType = Identify::IdentifyTypeEnum::kVisibleIndicator;

const EmberAfDeviceType gOnOffDeviceTypes[] = { { DEVICE_TYPE_LO_ON_OFF_PLUG, DEVICE_VERSION_DEFAULT } };

static EndpointId gCurrentEndpointId;
static EndpointId gFirstDynamicEndpointId;

static Device *gDevices[CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT + 1];

constexpr const EmberAfAttributeMinMaxValue MinMaxDefaultsArray[] = {
    { (uint16_t)0xFF, (uint16_t)0x0, (uint16_t)0x2 }, // StartUpOnOffMinMaxDefaults
    { (uint16_t)0x0, (uint16_t)0x0, (uint16_t)0x3 }   // OptionsMinMaxDefaults
};

#define DECLARE_DYNAMIC_ATTRIBUTE_WITH_MINMAX(attId, attType, attSizeBytes, attrMask, minMaxIndex)                                                          \
    {                                                                                                                              \
        &MinMaxDefaultsArray[minMaxIndex], attId, attSizeBytes, ZAP_TYPE(attType), attrMask | ZAP_ATTRIBUTE_MASK(EXTERNAL_STORAGE)               \
    }
// ---------------------------------------------------------------------------
//
// Plugin ENDPOINT: contains the following clusters:
//   - On/Off
//   - Descriptor
//   - Identify
//   - Groups

// Declare On/Off cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(onOffAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnOff::Id, BOOLEAN, 1, ZAP_ATTRIBUTE_MASK(TOKENIZE)), /* OnOff */
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::GlobalSceneControl::Id, BOOLEAN, 1, 0), /* GlobalSceneControl */
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OnTime::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* OnTime */
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::OffWaitTime::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* OffWaitTime */
    DECLARE_DYNAMIC_ATTRIBUTE_WITH_MINMAX(OnOff::Attributes::StartUpOnOff::Id, ENUM8, 1, ZAP_ATTRIBUTE_MASK(MIN_MAX) | ZAP_ATTRIBUTE_MASK(TOKENIZE) | ZAP_ATTRIBUTE_MASK(WRITABLE) | ZAP_ATTRIBUTE_MASK(NULLABLE), 0), /* StartUpOnOff */
    DECLARE_DYNAMIC_ATTRIBUTE(OnOff::Attributes::FeatureMap::Id, BITMAP32, 4, 0), /* FeatureMap */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Identify cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(identifyAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(Identify::Attributes::IdentifyTime::Id, INT16U, 2, ZAP_ATTRIBUTE_MASK(WRITABLE)), /* IdentifyTime */
    DECLARE_DYNAMIC_ATTRIBUTE(Identify::Attributes::IdentifyType::Id, ENUM8, 1, 0), /* IdentifyType */
    DECLARE_DYNAMIC_ATTRIBUTE(Identify::Attributes::FeatureMap::Id, BITMAP32, 4, 0), /* FeatureMap */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Descriptor cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(descriptorAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::DeviceTypeList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* DeviceTypeList */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ServerList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* ServerList */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::ClientList::Id, ARRAY, kDescriptorAttributeArraySize, 0), /* ClientList */
    DECLARE_DYNAMIC_ATTRIBUTE(Descriptor::Attributes::PartsList::Id, ARRAY, kDescriptorAttributeArraySize, 0),  /* PartsList */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Groups cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(groupsAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(Groups::Attributes::NameSupport::Id, BITMAP8, 1, 0), /* NameSupport */
    DECLARE_DYNAMIC_ATTRIBUTE(Groups::Attributes::FeatureMap::Id, BITMAP32, 4, 0), /* FeatureMap */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

// Declare Level Control cluster attributes
DECLARE_DYNAMIC_ATTRIBUTE_LIST_BEGIN(levelControlAttrs)
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::CurrentLevel::Id, INT8U, 1, ZAP_ATTRIBUTE_MASK(TOKENIZE) | ZAP_ATTRIBUTE_MASK(NULLABLE)), /* CurrentLevel */
    DECLARE_DYNAMIC_ATTRIBUTE_WITH_MINMAX(LevelControl::Attributes::Options::Id, BITMAP8, 1, ZAP_ATTRIBUTE_MASK(MIN_MAX) | ZAP_ATTRIBUTE_MASK(WRITABLE), 1), /* Options */
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::OnLevel::Id, INT8U, 1, ZAP_ATTRIBUTE_MASK(WRITABLE) | ZAP_ATTRIBUTE_MASK(NULLABLE)), /* OnLevel */
    DECLARE_DYNAMIC_ATTRIBUTE(LevelControl::Attributes::FeatureMap::Id, BITMAP32, 4, 0), /* FeatureMap */
DECLARE_DYNAMIC_ATTRIBUTE_LIST_END();

constexpr CommandId onOffIncomingCommands[] = {
    app::Clusters::OnOff::Commands::Off::Id,
    app::Clusters::OnOff::Commands::On::Id,
    app::Clusters::OnOff::Commands::Toggle::Id,
    app::Clusters::OnOff::Commands::OffWithEffect::Id,
    app::Clusters::OnOff::Commands::OnWithRecallGlobalScene::Id,
    app::Clusters::OnOff::Commands::OnWithTimedOff::Id,
    kInvalidCommandId,
};

constexpr CommandId identifyIncomingCommands[] = {
    app::Clusters::Identify::Commands::Identify::Id,
    app::Clusters::Identify::Commands::TriggerEffect::Id,
    kInvalidCommandId,
};

constexpr CommandId groupsIncomingCommands[] = {
    app::Clusters::Groups::Commands::AddGroup::Id,
    app::Clusters::Groups::Commands::ViewGroup::Id,
    app::Clusters::Groups::Commands::GetGroupMembership::Id,
    app::Clusters::Groups::Commands::RemoveGroup::Id,
    app::Clusters::Groups::Commands::RemoveAllGroups::Id,
    app::Clusters::Groups::Commands::AddGroupIfIdentifying::Id,
    kInvalidCommandId,
};

constexpr CommandId groupsOutgoingCommands[] = {
    app::Clusters::Groups::Commands::AddGroupResponse::Id,
    app::Clusters::Groups::Commands::ViewGroupResponse::Id,
    app::Clusters::Groups::Commands::GetGroupMembershipResponse::Id,
    app::Clusters::Groups::Commands::RemoveGroupResponse::Id,
    kInvalidCommandId,
};

constexpr CommandId levelControlIncomingCommands[] = {
    app::Clusters::LevelControl::Commands::MoveToLevel::Id,
    app::Clusters::LevelControl::Commands::Move::Id,
    app::Clusters::LevelControl::Commands::Step::Id,
    app::Clusters::LevelControl::Commands::Stop::Id,
    app::Clusters::LevelControl::Commands::MoveToLevelWithOnOff::Id,
    app::Clusters::LevelControl::Commands::MoveWithOnOff::Id,
    app::Clusters::LevelControl::Commands::StepWithOnOff::Id,
    app::Clusters::LevelControl::Commands::StopWithOnOff::Id,
    kInvalidCommandId,
};

const EmberAfGenericClusterFunction chipFuncArrayOnOffServer[] = {                                                             \
        (EmberAfGenericClusterFunction) emberAfOnOffClusterServerInitCallback,                                                     \
        (EmberAfGenericClusterFunction) MatterOnOffClusterServerShutdownCallback,                                                  \
    };

const EmberAfGenericClusterFunction chipFuncArrayIdentifyServer[] = {                                                          \
    (EmberAfGenericClusterFunction) emberAfIdentifyClusterServerInitCallback,                                                  \
    (EmberAfGenericClusterFunction) MatterIdentifyClusterServerAttributeChangedCallback,                                       \
};

const EmberAfGenericClusterFunction chipFuncArrayLevelControlServer[] = {                                                      \
    (EmberAfGenericClusterFunction) emberAfLevelControlClusterServerInitCallback,                                              \
    (EmberAfGenericClusterFunction) MatterLevelControlClusterServerShutdownCallback,                                           \
};

#define DECLARE_DYNAMIC_ONOFF_CLUSTER(clusterId, clusterAttrs, role, incomingCommands, outgoingCommands)                                 \
    {                                                                                                                              \
        clusterId, clusterAttrs, ArraySize(clusterAttrs), 0, role, chipFuncArrayOnOffServer, incomingCommands, outgoingCommands                        \
    }

#define DECLARE_DYNAMIC_IDENTIFY_CLUSTER(clusterId, clusterAttrs, role, incomingCommands, outgoingCommands)                                 \
    {                                                                                                                              \
        clusterId, clusterAttrs, ArraySize(clusterAttrs), 0, role, chipFuncArrayIdentifyServer, incomingCommands, outgoingCommands                        \
    }

#define DECLARE_DYNAMIC_LEVEL_CONTROL_CLUSTER(clusterId, clusterAttrs, role, incomingCommands, outgoingCommands)                                 \
    {                                                                                                                              \
        clusterId, clusterAttrs, ArraySize(clusterAttrs), 0, role, chipFuncArrayLevelControlServer, incomingCommands, outgoingCommands                        \
    }

// Declare Cluster List for Dynamic Plugin endpoint
DECLARE_DYNAMIC_CLUSTER_LIST_BEGIN(dynamicPluginClusters)
    DECLARE_DYNAMIC_ONOFF_CLUSTER(OnOff::Id, onOffAttrs, ZAP_CLUSTER_MASK(SERVER) | ZAP_CLUSTER_MASK(INIT_FUNCTION) | ZAP_CLUSTER_MASK(SHUTDOWN_FUNCTION), onOffIncomingCommands, nullptr),
    DECLARE_DYNAMIC_CLUSTER(Descriptor::Id, descriptorAttrs, ZAP_CLUSTER_MASK(SERVER), nullptr, nullptr),
    DECLARE_DYNAMIC_IDENTIFY_CLUSTER(Identify::Id, identifyAttrs, ZAP_CLUSTER_MASK(SERVER) | ZAP_CLUSTER_MASK(INIT_FUNCTION) | ZAP_CLUSTER_MASK(ATTRIBUTE_CHANGED_FUNCTION), identifyIncomingCommands, nullptr),
    DECLARE_DYNAMIC_IDENTIFY_CLUSTER(Groups::Id, groupsAttrs, ZAP_CLUSTER_MASK(SERVER) | ZAP_CLUSTER_MASK(INIT_FUNCTION), groupsIncomingCommands, groupsOutgoingCommands),
    DECLARE_DYNAMIC_LEVEL_CONTROL_CLUSTER(LevelControl::Id, levelControlAttrs, ZAP_CLUSTER_MASK(SERVER) | ZAP_CLUSTER_MASK(INIT_FUNCTION) | ZAP_CLUSTER_MASK(SHUTDOWN_FUNCTION), levelControlIncomingCommands, nullptr),
DECLARE_DYNAMIC_CLUSTER_LIST_END;

// Declare Dynamic Plugin endpoint
DECLARE_DYNAMIC_ENDPOINT(dynamicPluginEndpoint, dynamicPluginClusters);
DataVersion gPlugin1DataVersions[ArraySize(dynamicPluginClusters)];

DeviceOnOff Plugin1("Dynamic Plugin 1", "Office");
}

extern void emAfCallInitsExternal(chip::EndpointId endpointId);
// extern void MatterDescriptorPluginServerInitCallback();
extern void emAfSaveAttributeToStorageIfNeededExternal(uint8_t * data, chip::EndpointId endpoint, chip::ClusterId clusterId,
    const EmberAfAttributeMetadata * metadata);

int AddDeviceEndpoint(Device* dev, EmberAfEndpointType* ep, const Span<const EmberAfDeviceType>& deviceTypeList,
    const Span<DataVersion>& dataVersionStorage, chip::EndpointId parentEndpointId = chip::kInvalidEndpointId)
{
    uint8_t index = 0;
    while (index < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        if (nullptr == gDevices[index])
        {
            gDevices[index] = dev;
            CHIP_ERROR err;
            while (true)
            {
                // Todo: Update this to schedule the work rather than use this lock
                DeviceLayer::StackLock lock;
                dev->SetEndpointId(gCurrentEndpointId);
                dev->SetParentEndpointId(parentEndpointId);
                err =
                    emberAfSetDynamicEndpoint(index, gCurrentEndpointId, ep, dataVersionStorage, deviceTypeList, parentEndpointId);
                if (err == CHIP_NO_ERROR)
                {
                    ChipLogProgress(DeviceLayer, "Added device %s to dynamic endpoint %d (index=%d)", dev->GetName(),
                        gCurrentEndpointId, index);

                    if (dev->GetUniqueId()[0] == '\0')
                    {
                        dev->GenerateUniqueId();
                    }

                    return index;
                }
                if (err != CHIP_ERROR_ENDPOINT_EXISTS)
                {
                    gDevices[index] = nullptr;
                    return -1;
                }
                // Handle wrap condition
                if (++gCurrentEndpointId < gFirstDynamicEndpointId)
                {
                    gCurrentEndpointId = gFirstDynamicEndpointId;
                }
            }
        }
        index++;
    }
    ChipLogProgress(DeviceLayer, "Failed to add dynamic endpoint: No endpoints available!");
    return -1;
}

bool AppTask::IsTurnedOn() const
{
    return sfixture_on;
}

#ifdef CONFIG_CHIP_ENABLE_POWER_ON_FACTORY_RESET
void AppTask::PowerOnFactoryReset(void)
{
    LOG_INF("Lighting App Power On Factory Reset");
    AppEvent event;
    event.Type    = AppEvent::kEventType_DeviceAction;
    event.Handler = PowerOnFactoryResetEventHandler;
    GetAppTask().PostEvent(&event);
}
#endif /* CONFIG_CHIP_ENABLE_POWER_ON_FACTORY_RESET */

CHIP_ERROR AppTask::Init(void)
{
    SetExampleButtonCallbacks(LightingActionEventHandler);
    InitCommonParts();

    InitDynamicEndpoints();

    Protocols::InteractionModel::Status status;

    app::DataModel::Nullable<uint8_t> brightness;
    // Read brightness value
    status = Clusters::LevelControl::Attributes::CurrentLevel::Get(kExampleEndpointId, brightness);
    if (status == Protocols::InteractionModel::Status::Success && !brightness.IsNull())
    {
        sBrightness = brightness.Value();
    }

    memset(&sLedRgb, sBrightness, sizeof(RgbColor_t));

    bool storedValue;
    // Read storedValue on/off value
    status = Clusters::OnOff::Attributes::OnOff::Get(1, &storedValue);
    if (status == Protocols::InteractionModel::Status::Success)
    {
        // Set actual state to stored before reboot
        SetInitiateAction(storedValue ? ON_ACTION : OFF_ACTION, static_cast<int32_t>(AppEvent::kEventType_DeviceAction), nullptr);
    }

    return CHIP_NO_ERROR;
}

void AppTask::InitDynamicEndpoints(void)
{
    uint16_t dynamicEndpointCount = CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT;
    LOG_INF("dynamicEndpointCount = %d", dynamicEndpointCount);

    uint16_t fixedEndpointCount = emberAfFixedEndpointCount();
    LOG_INF("fixedEndpointCount = %d", fixedEndpointCount);

    Plugin1.SetReachable(true);

    for (size_t i = 0; i < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT; i++)
    {
        gDevices[i] = nullptr;
    }

    gFirstDynamicEndpointId = static_cast<chip::EndpointId>(
        static_cast<int>(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1))) + 1);

    LOG_INF("FirstDynamicEndpointId = %d", gFirstDynamicEndpointId);

    gCurrentEndpointId = gFirstDynamicEndpointId;

    // Disable last fixed endpoint, which is used as a placeholder for all of the
    // supported clusters so that ZAP will generated the requisite code.
    emberAfEndpointEnableDisable(emberAfEndpointFromIndex(static_cast<uint16_t>(emberAfFixedEndpointCount() - 1)), false);

    // Add Plugin 1 -> will be mapped to ZCL endpoints 3
    AddDeviceEndpoint(&Plugin1, &dynamicPluginEndpoint, Span<const EmberAfDeviceType>(gOnOffDeviceTypes),
                      Span<DataVersion>(gPlugin1DataVersions), 1);

    emberAfInitializeAttributesExternal(gCurrentEndpointId);
    // MatterDescriptorPluginServerInitCallback();
    emAfCallInitsExternal(gCurrentEndpointId);
    LOG_INF("InitDynamicEndpoints: Done");
}

Protocols::InteractionModel::Status HandleReadOnOffAttribute(DeviceOnOff* dev, chip::AttributeId attributeId, uint8_t* buffer,
    uint16_t maxReadLength)
{
    ChipLogProgress(DeviceLayer, "HandleReadOnOffAttribute: attrId = %d, maxReadLength = %d", attributeId, maxReadLength);

    if ((attributeId == OnOff::Attributes::OnOff::Id) && (maxReadLength == 1))
    {
        *buffer = dev->IsOn() ? 1 : 0;
    }
    else if ((attributeId == OnOff::Attributes::ClusterRevision::Id) && (maxReadLength == 2))
    {
        uint16_t rev = ZCL_ON_OFF_CLUSTER_REVISION;
        memcpy(buffer, &rev, sizeof(rev));
    }
    else if ((attributeId == OnOff::Attributes::GlobalSceneControl::Id) && (maxReadLength == 1))
    {
        *buffer = dev->IsGlobalSceneControl() ? 1 : 0;
    }
    else if ((attributeId == OnOff::Attributes::OnTime::Id) && (maxReadLength == 2))
    {
        *buffer = dev->DeviceOnOff::GetOnTime();
    }
    else if ((attributeId == OnOff::Attributes::OffWaitTime::Id) && (maxReadLength == 2))
    {
        *buffer = dev->DeviceOnOff::GetOffWaitTime();
    }
    else if ((attributeId == OnOff::Attributes::FeatureMap::Id) && (maxReadLength == 4))
    {
        uint32_t featureMap = ZCL_ON_OFF_FEATURE_MAP;
        memcpy(buffer, &featureMap, sizeof(featureMap));
    }
    else if ((attributeId == OnOff::Attributes::StartUpOnOff::Id) && (maxReadLength == 1))
    {
        chip::app::DataModel::Nullable<chip::app::Clusters::OnOff::StartUpOnOffEnum> startupOnOff = dev->GetStartUpOnOff();
        *buffer = startupOnOff.IsNull() ? 0xFF : static_cast<uint8_t>(startupOnOff.Value());
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleReadIdentifyAttribute(DeviceOnOff* dev, chip::AttributeId attributeId, uint8_t* buffer,
    uint16_t maxReadLength)
{
    ChipLogProgress(DeviceLayer, "HandleReadIdentifyAttribute: attrId = %d, maxReadLength = %d", attributeId, maxReadLength);

    if ((attributeId == Identify::Attributes::IdentifyTime::Id) && (maxReadLength == 2))
    {
        *buffer = dev->DeviceOnOff::GetIdentifyTime();
    }
    else if ((attributeId == Identify::Attributes::ClusterRevision::Id) && (maxReadLength == 2))
    {
        uint16_t rev = ZCL_IDENTIFY_CLUSTER_REVISION;
        memcpy(buffer, &rev, sizeof(rev));
    }
    else if ((attributeId == Identify::Attributes::FeatureMap::Id) && (maxReadLength == 4))
    {
        uint32_t featureMap = ZCL_IDENTIFY_FEATURE_MAP;
        memcpy(buffer, &featureMap, sizeof(featureMap));
    }
    else if ((attributeId == Identify::Attributes::IdentifyType::Id) && (maxReadLength == 1))
    {
        *buffer = static_cast<uint8_t>(kIdentifyType);
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleReadGroupsAttribute(DeviceOnOff* dev, chip::AttributeId attributeId, uint8_t* buffer,
    uint16_t maxReadLength)
{
    ChipLogProgress(DeviceLayer, "HandleReadGroupsAttribute: attrId = %d, maxReadLength = %d", attributeId, maxReadLength);

    if ((attributeId == Groups::Attributes::NameSupport::Id) && (maxReadLength == 1))
    {
        *buffer = static_cast<uint8_t>(kNameSupport);
    }
    else if ((attributeId == Groups::Attributes::ClusterRevision::Id) && (maxReadLength == 2))
    {
        uint16_t rev = ZCL_GROUPS_CLUSTER_REVISION;
        memcpy(buffer, &rev, sizeof(rev));
    }
    else if ((attributeId == Groups::Attributes::FeatureMap::Id) && (maxReadLength == 4))
    {
        uint32_t featureMap = ZCL_GROUPS_FEATURE_MAP;
        memcpy(buffer, &featureMap, sizeof(featureMap));
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleReadLevelControlAttribute(DeviceOnOff* dev, chip::AttributeId attributeId, uint8_t* buffer,
    uint16_t maxReadLength)
{
    ChipLogProgress(DeviceLayer, "HandleReadLevelControlAttribute: attrId = %d, maxReadLength = %d", attributeId, maxReadLength);

    if ((attributeId == LevelControl::Attributes::CurrentLevel::Id) && (maxReadLength == 1))
    {
        chip::app::DataModel::Nullable<uint8_t> currentLevel = dev->GetCurrentLevel();
        *buffer = currentLevel.IsNull() ? 0xFF : currentLevel.Value();
    }
    else if ((attributeId == LevelControl::Attributes::Options::Id) && (maxReadLength == 1))
    {
        *buffer = dev->DeviceOnOff::GetOptions();
    }
    else if ((attributeId == LevelControl::Attributes::OnLevel::Id) && (maxReadLength == 1))
    {
        chip::app::DataModel::Nullable<uint8_t> onLevel = dev->GetOnLevel();
        *buffer = onLevel.IsNull() ? 0xFF : onLevel.Value();
    }
    else if ((attributeId == LevelControl::Attributes::ClusterRevision::Id) && (maxReadLength == 2))
    {
        uint16_t rev = ZCL_LEVEL_CONTROL_CLUSTER_REVISION;
        memcpy(buffer, &rev, sizeof(rev));
    }
    else if ((attributeId == LevelControl::Attributes::FeatureMap::Id) && (maxReadLength == 4))
    {
        uint32_t featureMap = ZCL_LEVEL_CONTROL_FEATURE_MAP;
        memcpy(buffer, &featureMap, sizeof(featureMap));
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status emberAfExternalAttributeReadCallback(EndpointId endpoint, ClusterId clusterId,
    const EmberAfAttributeMetadata* attributeMetadata,
    uint8_t* buffer, uint16_t maxReadLength)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    ChipLogProgress(DeviceLayer, "emberAfExternalAttributeReadCallback: ep = %d", endpoint);

    Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;

    if ((endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT) && (gDevices[endpointIndex] != nullptr))
    {
        Device* dev = gDevices[endpointIndex];

        if (clusterId == OnOff::Id)
        {
            ret = HandleReadOnOffAttribute(static_cast<DeviceOnOff*>(dev), attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else if (clusterId == Identify::Id)
        {
            ret = HandleReadIdentifyAttribute(static_cast<DeviceOnOff*>(dev), attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else if (clusterId == Groups::Id)
        {
            ret = HandleReadGroupsAttribute(static_cast<DeviceOnOff*>(dev), attributeMetadata->attributeId, buffer, maxReadLength);
        }
        else if (clusterId == LevelControl::Id)
        {
            ret = HandleReadLevelControlAttribute(static_cast<DeviceOnOff*>(dev), attributeMetadata->attributeId, buffer, maxReadLength);
        }
    }

    return ret;
}

void OnIdentifyTriggerEffect(::Identify * identify)
{
    AppTaskCommon::IdentifyEffectHandler(identify->mCurrentEffectIdentifier);
}

struct Identify sIdentify0 = {
    kFirstDynamicEndpointId,           AppTaskCommon::IdentifyStartHandler,
    AppTaskCommon::IdentifyStopHandler, Clusters::Identify::IdentifyTypeEnum::kVisibleIndicator,
    OnIdentifyTriggerEffect,
};

Protocols::InteractionModel::Status HandleWriteOnOffAttribute(DeviceOnOff * dev, chip::AttributeId attributeId, uint8_t * buffer)
{
    ChipLogProgress(DeviceLayer, "HandleWriteOnOffAttribute: attrId = %d", attributeId);

    if ((attributeId == OnOff::Attributes::OnOff::Id) && (dev->IsReachable()))
    {
        if (*buffer)
        {
            dev->SetOnOff(true);
        }
        else
        {
            dev->SetOnOff(false);
        }
    }
    else if ((attributeId == OnOff::Attributes::OnTime::Id) && (dev->IsReachable()))
    {
        uint16_t onTime;
        memcpy(&onTime, buffer, sizeof(onTime));
        dev->SetOnTime(onTime);
    }
    else if ((attributeId == OnOff::Attributes::OffWaitTime::Id) && (dev->IsReachable()))
    {
        uint16_t offWaitTime;
        memcpy(&offWaitTime, buffer, sizeof(offWaitTime));
        dev->SetOffWaitTime(offWaitTime);
    }
    else if ((attributeId == OnOff::Attributes::StartUpOnOff::Id) && (dev->IsReachable()))
    {
        ChipLogProgress(DeviceLayer, "Received StartUpOnOff value: %d", *buffer);
        chip::app::DataModel::Nullable<chip::app::Clusters::OnOff::StartUpOnOffEnum> startupOnOff;
        if (*buffer == 0xFF)
        {
            startupOnOff.SetNull();
        }
        else
        {
            startupOnOff.SetNonNull(static_cast<chip::app::Clusters::OnOff::StartUpOnOffEnum>(*buffer));
        }
        dev->SetStartUpOnOff(startupOnOff);
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleWriteIdentifyAttribute(DeviceOnOff * dev, chip::AttributeId attributeId, uint8_t * buffer)
{
    ChipLogProgress(DeviceLayer, "HandleWriteIdentifyAttribute: attrId = %d", attributeId);

    if ((attributeId == Identify::Attributes::IdentifyTime::Id) && (dev->IsReachable()))
    {
        uint16_t identifyTime;
        memcpy(&identifyTime, buffer, sizeof(identifyTime));
        dev->SetIdentifyTime(identifyTime);
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status HandleWriteLevelControlAttribute(DeviceOnOff * dev, chip::AttributeId attributeId, uint8_t * buffer)
{
    ChipLogProgress(DeviceLayer, "HandleWriteLevelControlAttribute: attrId = %d", attributeId);

    if ((attributeId == LevelControl::Attributes::CurrentLevel::Id) && (dev->IsReachable()))
    {
        chip::app::DataModel::Nullable<uint8_t> currentLevel;
        if (*buffer == 0xFF)
        {
            currentLevel.SetNull();
        }
        else
        {
            currentLevel.SetNonNull(*buffer);
        }
        dev->SetCurrentLevel(currentLevel);
    }
    else if ((attributeId == LevelControl::Attributes::Options::Id) && (dev->IsReachable()))
    {
        dev->SetOptions(*buffer);
    }
    else if ((attributeId == LevelControl::Attributes::OnLevel::Id) && (dev->IsReachable()))
    {
        chip::app::DataModel::Nullable<uint8_t> onLevel;
        if (*buffer == 0xFF)
        {
            onLevel.SetNull();
        }
        else
        {
            onLevel.SetNonNull(*buffer);
        }
        dev->SetOnLevel(onLevel);
    }
    else
    {
        return Protocols::InteractionModel::Status::Failure;
    }

    return Protocols::InteractionModel::Status::Success;
}

Protocols::InteractionModel::Status emberAfExternalAttributeWriteCallback(EndpointId endpoint, ClusterId clusterId,
    const EmberAfAttributeMetadata* attributeMetadata,
    uint8_t* buffer)
{
    uint16_t endpointIndex = emberAfGetDynamicIndexFromEndpoint(endpoint);

    Protocols::InteractionModel::Status ret = Protocols::InteractionModel::Status::Failure;

    ChipLogProgress(DeviceLayer, "emberAfExternalAttributeWriteCallback: ep = %d", endpoint);

    if (endpointIndex < CHIP_DEVICE_CONFIG_DYNAMIC_ENDPOINT_COUNT)
    {
        Device* dev = gDevices[endpointIndex];

        if ((dev->IsReachable()) && (clusterId == OnOff::Id))
        {
            ret = HandleWriteOnOffAttribute(static_cast<DeviceOnOff*>(dev), attributeMetadata->attributeId, buffer);
        }
        else if ((dev->IsReachable()) && (clusterId == Identify::Id))
        {
            ret = HandleWriteIdentifyAttribute(static_cast<DeviceOnOff*>(dev), attributeMetadata->attributeId, buffer);
        }
        else if ((dev->IsReachable()) && (clusterId == LevelControl::Id))
        {
            ret = HandleWriteLevelControlAttribute(static_cast<DeviceOnOff*>(dev), attributeMetadata->attributeId, buffer);
        }

        emAfSaveAttributeToStorageIfNeededExternal(buffer, endpoint, clusterId, attributeMetadata);
    }

    return ret;
}

void AppTask::LightingActionEventHandler(AppEvent * aEvent)
{
    Fixture_Action action = INVALID_ACTION;
    int32_t actor         = 0;

    if (aEvent->Type == AppEvent::kEventType_DeviceAction)
    {
        action = static_cast<Fixture_Action>(aEvent->DeviceEvent.Action);
        actor  = aEvent->DeviceEvent.Actor;
    }
    else if (aEvent->Type == AppEvent::kEventType_Button)
    {
        sfixture_on = !sfixture_on;

        sAppTask.UpdateClusterState();
    }
}

void AppTask::UpdateClusterState(void)
{
    Protocols::InteractionModel::Status status;
    bool isTurnedOn  = sfixture_on;
    uint8_t setLevel = sBrightness;

    // write the new on/off value
    status = Clusters::OnOff::Attributes::OnOff::Set(kExampleEndpointId, isTurnedOn);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        LOG_ERR("Update OnOff fail: %x", to_underlying(status));
    }

    status = Clusters::LevelControl::Attributes::CurrentLevel::Set(kExampleEndpointId, setLevel);
    if (status != Protocols::InteractionModel::Status::Success)
    {
        LOG_ERR("Update CurrentLevel fail: %x", to_underlying(status));
    }
}

void AppTask::SetInitiateAction(Fixture_Action aAction, int32_t aActor, uint8_t * value)
{
    bool setRgbAction = false;

    if (aAction == ON_ACTION || aAction == OFF_ACTION)
    {
        if (aAction == ON_ACTION)
        {
            sfixture_on = true;
#ifdef CONFIG_PWM
            PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Red, (((uint32_t) sLedRgb.r * 1000) / UINT8_MAX));
            PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Green, (((uint32_t) sLedRgb.g * 1000) / UINT8_MAX));
            PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Blue, (((uint32_t) sLedRgb.b * 1000) / UINT8_MAX));
#else
            LedManager::getInstance().setLed(LedManager::EAppLed_App0, true);
#endif
        }
        else
        {
            sfixture_on = false;
#ifdef CONFIG_PWM
            PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Red, false);
            PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Green, false);
            PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Blue, false);
#else
            LedManager::getInstance().setLed(LedManager::EAppLed_App0, false);
#endif
        }
    }
    else if (aAction == LEVEL_ACTION)
    {
        // Save a new brightness for ColorControl
        sBrightness = *value;

        if (sColorAction == COLOR_ACTION_XY)
        {
            sLedRgb = XYToRgb(sBrightness, sXY.x, sXY.y);
        }
        else if (sColorAction == COLOR_ACTION_HSV)
        {
            sHSV.v  = sBrightness;
            sLedRgb = HsvToRgb(sHSV);
        }
        else
        {
            memset(&sLedRgb, sBrightness, sizeof(RgbColor_t));
        }

        ChipLogProgress(Zcl, "New brightness: %u | R: %u, G: %u, B: %u", sBrightness, sLedRgb.r, sLedRgb.g, sLedRgb.b);
        setRgbAction = true;
    }
    else if (aAction == COLOR_ACTION_XY)
    {
        sXY     = *reinterpret_cast<XyColor_t *>(value);
        sLedRgb = XYToRgb(sBrightness, sXY.x, sXY.y);
        ChipLogProgress(Zcl, "XY to RGB: X: %u, Y: %u, Level: %u | R: %u, G: %u, B: %u", sXY.x, sXY.y, sBrightness, sLedRgb.r,
                        sLedRgb.g, sLedRgb.b);
        setRgbAction = true;
        sColorAction = COLOR_ACTION_XY;
    }
    else if (aAction == COLOR_ACTION_HSV)
    {
        sHSV    = *reinterpret_cast<HsvColor_t *>(value);
        sHSV.v  = sBrightness;
        sLedRgb = HsvToRgb(sHSV);
        ChipLogProgress(Zcl, "HSV to RGB: H: %u, S: %u, V: %u | R: %u, G: %u, B: %u", sHSV.h, sHSV.s, sHSV.v, sLedRgb.r, sLedRgb.g,
                        sLedRgb.b);
        setRgbAction = true;
        sColorAction = COLOR_ACTION_HSV;
    }
    else if (aAction == COLOR_ACTION_CT)
    {
        sCT = *reinterpret_cast<CtColor_t *>(value);
        if (sCT.ctMireds)
        {
            sLedRgb = CTToRgb(sCT);
            ChipLogProgress(Zcl, "ColorTemp to RGB: CT: %u | R: %u, G: %u, B: %u", sCT.ctMireds, sLedRgb.r, sLedRgb.g, sLedRgb.b);
            setRgbAction = true;
            sColorAction = COLOR_ACTION_CT;
        }
    }

    if (setRgbAction)
    {
        PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Red, (((uint32_t) sLedRgb.r * 1000) / UINT8_MAX));
        PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Green, (((uint32_t) sLedRgb.g * 1000) / UINT8_MAX));
        PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Blue, (((uint32_t) sLedRgb.b * 1000) / UINT8_MAX));
    }
}

#ifdef CONFIG_CHIP_ENABLE_POWER_ON_FACTORY_RESET
static constexpr uint32_t kPowerOnFactoryResetIndicationMax    = 4;
static constexpr uint32_t kPowerOnFactoryResetIndicationTimeMs = 1000;

unsigned int AppTask::sPowerOnFactoryResetTimerCnt;
k_timer AppTask::sPowerOnFactoryResetTimer;

void AppTask::PowerOnFactoryResetEventHandler(AppEvent * aEvent)
{
    LOG_INF("Lighting App Power On Factory Reset Handler");
    sPowerOnFactoryResetTimerCnt = 1;
    PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Red, (bool) (sPowerOnFactoryResetTimerCnt % 2));
    PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Green, (bool) (sPowerOnFactoryResetTimerCnt % 2));
    PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Blue, (bool) (sPowerOnFactoryResetTimerCnt % 2));
#if !CONFIG_PWM
    LedManager::getInstance().setLed(LedManager::EAppLed_App0, (bool) (sPowerOnFactoryResetTimerCnt % 2));
#endif
    k_timer_init(&sPowerOnFactoryResetTimer, PowerOnFactoryResetTimerEvent, nullptr);
    k_timer_start(&sPowerOnFactoryResetTimer, K_MSEC(kPowerOnFactoryResetIndicationTimeMs),
                  K_MSEC(kPowerOnFactoryResetIndicationTimeMs));
}

void AppTask::PowerOnFactoryResetTimerEvent(struct k_timer * timer)
{
    sPowerOnFactoryResetTimerCnt++;
    LOG_INF("Lighting App Power On Factory Reset Handler %u", sPowerOnFactoryResetTimerCnt);
    PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Red, (bool) (sPowerOnFactoryResetTimerCnt % 2));
    PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Green, (bool) (sPowerOnFactoryResetTimerCnt % 2));
    PwmManager::getInstance().setPwm(PwmManager::EAppPwm_Blue, (bool) (sPowerOnFactoryResetTimerCnt % 2));
    if (sPowerOnFactoryResetTimerCnt > kPowerOnFactoryResetIndicationMax)
    {
        k_timer_stop(timer);
        LOG_INF("schedule factory reset");
        chip::Server::GetInstance().ScheduleFactoryReset();
    }
}
#endif /* CONFIG_CHIP_ENABLE_POWER_ON_FACTORY_RESET */

void AppTask::LinkLeds(LedManager & ledManager)
{
#if CONFIG_CHIP_ENABLE_APPLICATION_STATUS_LED
    ledManager.linkLed(LedManager::EAppLed_Status, 0);
#endif

#if !CONFIG_PWM
    ledManager.linkLed(LedManager::EAppLed_App0, 1);
#endif /* !CONFIG_PWM */
}
