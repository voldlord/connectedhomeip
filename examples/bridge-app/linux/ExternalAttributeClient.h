/*
 *
 *    Copyright (c) 2024 Project CHIP Authors
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

#pragma once

#include <app/ReadClient.h>
#include <lib/core/CHIPError.h>
#include <lib/core/DataModelTypes.h>
#include <platform/CHIPDeviceLayer.h>
#include <vector>

namespace chip {
namespace bridge {

/**
 * @brief Manages attribute access (reads and subscriptions) from the bridge-app to other nodes on the fabric.
 * 
 * This class provides functionality to:
 * - Read attributes from remote nodes (one-time read)
 * - Create subscriptions to attributes on remote nodes (ongoing updates)
 * - Track active subscriptions and handle incoming subscription reports
 */
class ExternalAttributeClient
{
public:
    /**
     * @brief Get the singleton instance of ExternalAttributeClient
     */
    static ExternalAttributeClient & GetInstance();

    /**
     * @brief Read an attribute from a remote node (one-time read)
     * 
     * @param targetNodeId Node ID of the target device (e.g., EFD)
     * @param endpoint Endpoint ID (use 0xFFFF for wildcard - all endpoints)
     * @param cluster Cluster ID (use 0xFFFFFFFF for wildcard - all clusters)
     * @param attribute Attribute ID (use 0xFFFFFFFF for wildcard - all attributes)
     * @return CHIP_ERROR indicating success or failure
     */
    CHIP_ERROR ReadAttribute(NodeId targetNodeId, EndpointId endpoint, ClusterId cluster, AttributeId attribute);

    /**
     * @brief Subscribe to an attribute on a remote node (ongoing updates)
     * 
     * @param targetNodeId Node ID of the target device (e.g., EFD)
     * @param endpoint Endpoint ID (use 0xFFFF for wildcard - all endpoints)
     * @param cluster Cluster ID (use 0xFFFFFFFF for wildcard - all clusters)
     * @param attribute Attribute ID (use 0xFFFFFFFF for wildcard - all attributes)
     * @param minInterval Minimum reporting interval in seconds
     * @param maxInterval Maximum reporting interval in seconds
     * @return CHIP_ERROR indicating success or failure
     */
    CHIP_ERROR SubscribeToAttribute(NodeId targetNodeId, EndpointId endpoint, ClusterId cluster, AttributeId attribute,
                                    uint16_t minInterval, uint16_t maxInterval);

    /**
     * @brief List all active subscriptions to console
     */
    void ListSubscriptions();

    /**
     * @brief Cancel all active subscriptions (used during shutdown)
     */
    void CancelAllSubscriptions();

    struct SubscriptionContext
    {
        SubscriptionId subscriptionId;
        NodeId targetNodeId;
        EndpointId endpoint;
        ClusterId cluster;
        AttributeId attribute;
        ExternalAttributeClient * manager;

        SubscriptionContext(NodeId node, EndpointId ep, ClusterId cl, AttributeId attr, ExternalAttributeClient * mgr) :
            subscriptionId(0), targetNodeId(node), endpoint(ep), cluster(cl), attribute(attr), manager(mgr)
        {}
    };

    // Make RemoveSubscription public so SubscriptionCallback can call it
    void RemoveSubscription(SubscriptionId subId);

private:
    ExternalAttributeClient() = default;

    std::vector<SubscriptionContext *> mActiveSubscriptions;
};

} // namespace bridge
} // namespace chip
