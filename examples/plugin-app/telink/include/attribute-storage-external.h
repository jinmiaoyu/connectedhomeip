/**
 *
 *    Copyright (c) 2024 Project CHIP Authors
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

#pragma once

#include <app/util/af-types.h>
#include <app/util/att-storage.h>
#include <app/util/attribute-metadata.h>
#include <app/util/config.h>
#include <app/util/endpoint-config-defines.h>
#include <lib/support/CodeUtils.h>

#include <app-common/zap-generated/attribute-type.h>
#include <app-common/zap-generated/cluster-objects.h>

// NOTE: direct include here even though app/util/config.h is the public header,
//       because MAX_ENDPOINT_COUNT needs FIXED_ENDPOINT_COUNT definitions.
#include <zap-generated/endpoint_config.h>

void emberAfInitializeAttributesExternal(chip::EndpointId endpoint);