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

#include "ExternalAttributeClient.h"
#include <app/InteractionModelEngine.h>
#include <app/server/Server.h>
#include <lib/support/logging/CHIPLogging.h>

namespace chip {
namespace bridge {

// Helper function to log attribute value in a parseable format
static void LogAttributeValue(const char * prefix, const app::ConcreteDataAttributePath & aPath, TLV::TLVReader * apData)
{
    TLV::TLVReader reader(*apData);
    TLV::TLVType tlvType = reader.GetType();

    ChipLogProgress(NotSpecified, "%s EP=%u CL=0x%lx ATTR=0x%lx ", prefix, aPath.mEndpointId,
                    static_cast<unsigned long>(aPath.mClusterId), static_cast<unsigned long>(aPath.mAttributeId));

    switch (tlvType)
    {
    case TLV::kTLVType_Boolean: {
        bool value;
        if (reader.Get(value) == CHIP_NO_ERROR)
        {
            ChipLogProgress(NotSpecified, "  VALUE=%s", value ? "true" : "false");
        }
        break;
    }
    case TLV::kTLVType_SignedInteger: {
        int64_t value;
        if (reader.Get(value) == CHIP_NO_ERROR)
        {
            ChipLogProgress(NotSpecified, "  VALUE=%lld", static_cast<long long>(value));
        }
        break;
    }
    case TLV::kTLVType_UnsignedInteger: {
        uint64_t value;
        if (reader.Get(value) == CHIP_NO_ERROR)
        {
            ChipLogProgress(NotSpecified, "  VALUE=%llu", static_cast<unsigned long long>(value));
        }
        break;
    }
    case TLV::kTLVType_UTF8String: {
        CharSpan value;
        if (reader.Get(value) == CHIP_NO_ERROR)
        {
            ChipLogProgress(NotSpecified, "  VALUE=%.*s", static_cast<int>(value.size()), value.data());
        }
        break;
    }
    case TLV::kTLVType_Null: {
        ChipLogProgress(NotSpecified, "  VALUE=null");
        break;
    }
    case TLV::kTLVType_Array: {
        // For arrays, build a single-line string representation
        char arrayStr[256] = "[";
        size_t offset = 1;
        TLV::TLVType containerType;
        
        if (reader.EnterContainer(containerType) == CHIP_NO_ERROR)
        {
            bool first = true;
            while (reader.Next() != CHIP_END_OF_TLV && offset < sizeof(arrayStr) - 20)
            {
                if (!first && offset < sizeof(arrayStr) - 2)
                {
                    arrayStr[offset++] = ',';
                    arrayStr[offset++] = ' ';
                }
                first = false;
                
                TLV::TLVType elemType = reader.GetType();
                if (elemType == TLV::kTLVType_UnsignedInteger)
                {
                    uint64_t val;
                    if (reader.Get(val) == CHIP_NO_ERROR)
                    {
                        int written = snprintf(arrayStr + offset, sizeof(arrayStr) - offset, "%llu", 
                                              static_cast<unsigned long long>(val));
                        if (written > 0)
                        {
                            offset += static_cast<size_t>(written);
                        }
                    }
                }
                else if (elemType == TLV::kTLVType_SignedInteger)
                {
                    int64_t val;
                    if (reader.Get(val) == CHIP_NO_ERROR)
                    {
                        int written = snprintf(arrayStr + offset, sizeof(arrayStr) - offset, "%lld", 
                                              static_cast<long long>(val));
                        if (written > 0)
                        {
                            offset += static_cast<size_t>(written);
                        }
                    }
                }
                else
                {
                    int written = snprintf(arrayStr + offset, sizeof(arrayStr) - offset, "<?>");
                    if (written > 0)
                    {
                        offset += static_cast<size_t>(written);
                    }
                }
            }
            reader.ExitContainer(containerType);
            
            if (offset < sizeof(arrayStr) - 1)
            {
                arrayStr[offset++] = ']';
                arrayStr[offset] = '\0';
            }
            
            ChipLogProgress(NotSpecified, "  VALUE=%s", arrayStr);
        }
        break;
    }
    case TLV::kTLVType_Structure: {
        ChipLogProgress(NotSpecified, "  VALUE=<structure>");
        break;
    }
    default: {
        ChipLogProgress(NotSpecified, "  VALUE=<unknown type %u>", static_cast<unsigned>(tlvType));
        break;
    }
    }
}

// Callback implementation for one-time reads
class ReadCallback : public app::ReadClient::Callback
{
public:
    ReadCallback() {}

    void OnAttributeData(const app::ConcreteDataAttributePath & aPath, TLV::TLVReader * apData,
                         const app::StatusIB & aStatus) override
    {
        if (aStatus.IsSuccess() && apData != nullptr)
        {
            LogAttributeValue("[READ]", aPath, apData);
        }
        else
        {
            ChipLogError(NotSpecified, "[READ] EP=%u CL=0x%lx ATTR=0x%lx ERROR: Status=0x%x", aPath.mEndpointId,
                         static_cast<unsigned long>(aPath.mClusterId), static_cast<unsigned long>(aPath.mAttributeId),
                         aStatus.mStatus);
        }
    }

    void OnError(CHIP_ERROR aError) override 
    { 
        ChipLogError(NotSpecified, "Read error: %s", ErrorStr(aError)); 
    }

    void OnDone(app::ReadClient * apReadClient) override
    {
        ChipLogProgress(NotSpecified, "Read completed");
        
        // Delete the ReadClient
        Platform::Delete(apReadClient);
        
        // Delete ourselves
        Platform::Delete(this);
    }

    void OnDeallocatePaths(app::ReadPrepareParams && aReadPrepareParams) override
    {
        // Clean up the attribute path we allocated
        if (aReadPrepareParams.mpAttributePathParamsList != nullptr)
        {
            Platform::Delete(aReadPrepareParams.mpAttributePathParamsList);
        }
    }
};

// Callback implementation for subscriptions
class SubscriptionCallback : public app::ReadClient::Callback
{
public:
    SubscriptionCallback(ExternalAttributeClient::SubscriptionContext * context) : mContext(context) {}

    void OnAttributeData(const app::ConcreteDataAttributePath & aPath, TLV::TLVReader * apData,
                         const app::StatusIB & aStatus) override
    {
        if (aStatus.IsSuccess() && apData != nullptr)
        {
            LogAttributeValue("[SUBSCRIPTION]", aPath, apData);
        }
        else
        {
            ChipLogError(NotSpecified, "[SUBSCRIPTION] EP=%u CL=0x%lx ATTR=0x%lx ERROR: Status=0x%x", aPath.mEndpointId,
                         static_cast<unsigned long>(aPath.mClusterId), static_cast<unsigned long>(aPath.mAttributeId),
                         aStatus.mStatus);
        }
    }

    void OnSubscriptionEstablished(SubscriptionId aSubscriptionId) override
    {
        if (mContext)
        {
            mContext->subscriptionId = aSubscriptionId;
        }
        ChipLogProgress(NotSpecified, "Subscription established successfully: ID=0x%08lx",
                        static_cast<unsigned long>(aSubscriptionId));
    }

    void OnError(CHIP_ERROR aError) override { ChipLogError(NotSpecified, "Subscription error: %s", ErrorStr(aError)); }

    void OnDone(app::ReadClient * apReadClient) override
    {
        ChipLogProgress(NotSpecified, "Subscription completed/closed");

        // Clean up the context and callback
        if (mContext)
        {
            mContext->manager->RemoveSubscription(mContext->subscriptionId);
        }

        // Delete ourselves
        Platform::Delete(this);
    }

    void OnDeallocatePaths(app::ReadPrepareParams && aReadPrepareParams) override
    {
        // Clean up the attribute path we allocated
        if (aReadPrepareParams.mpAttributePathParamsList != nullptr)
        {
            Platform::Delete(aReadPrepareParams.mpAttributePathParamsList);
        }
    }

private:
    ExternalAttributeClient::SubscriptionContext * mContext;
};

ExternalAttributeClient & ExternalAttributeClient::GetInstance()
{
    static ExternalAttributeClient sInstance;
    return sInstance;
}

CHIP_ERROR ExternalAttributeClient::ReadAttribute(NodeId targetNodeId, EndpointId endpoint, ClusterId cluster,
                                                   AttributeId attribute)
{
    ChipLogProgress(NotSpecified, "Reading attribute from node 0x" ChipLogFormatX64 " EP=%u CL=0x%lx ATTR=0x%lx",
                    ChipLogValueX64(targetNodeId), endpoint, static_cast<unsigned long>(cluster),
                    static_cast<unsigned long>(attribute));

    // Get the server instance
    auto & server = Server::GetInstance();
    SessionManager * sessionMgr = server.GetExchangeManager().GetSessionManager();
    Messaging::ExchangeManager * exchangeMgr = &server.GetExchangeManager();
    
    // Find which fabric the target node is on by checking for existing sessions
    for (const auto & fabric : server.GetFabricTable())
    {
        ScopedNodeId candidatePeer(targetNodeId, fabric.GetFabricIndex());
        Optional<SessionHandle> session = sessionMgr->FindSecureSessionForNode(candidatePeer);
        
        if (!session.HasValue())
        {
            continue;
        }
        
        // Found a session! Use it immediately without storing
        ChipLogProgress(NotSpecified, "Found existing session to node on fabric %u", fabric.GetFabricIndex());

        // Create the read callback
        auto * readCallback = Platform::New<ReadCallback>();
        if (readCallback == nullptr)
        {
            return CHIP_ERROR_NO_MEMORY;
        }

        // Create ReadClient for one-time read
        auto * readClient = Platform::New<app::ReadClient>(app::InteractionModelEngine::GetInstance(), exchangeMgr,
                                                           *readCallback, app::ReadClient::InteractionType::Read);
        if (readClient == nullptr)
        {
            Platform::Delete(readCallback);
            return CHIP_ERROR_NO_MEMORY;
        }

        // Prepare read parameters - pass session directly to constructor
        app::ReadPrepareParams params(session.Value());
        params.mIsFabricFiltered = true;

        // Create attribute path - supports wildcards
        auto * attributePath = Platform::New<app::AttributePathParams>();
        if (attributePath == nullptr)
        {
            Platform::Delete(readClient);
            Platform::Delete(readCallback);
            return CHIP_ERROR_NO_MEMORY;
        }

        attributePath->mEndpointId  = endpoint;
        attributePath->mClusterId   = cluster;
        attributePath->mAttributeId = attribute;

        params.mpAttributePathParamsList    = attributePath;
        params.mAttributePathParamsListSize = 1;

        ChipLogProgress(NotSpecified, "Sending read request to node 0x" ChipLogFormatX64 " on fabric %u",
                        ChipLogValueX64(targetNodeId), fabric.GetFabricIndex());

        // Send read request
        CHIP_ERROR err = readClient->SendRequest(params);

        if (err != CHIP_NO_ERROR)
        {
            ChipLogError(NotSpecified, "Failed to send read request: %s", ErrorStr(err));
            Platform::Delete(readClient);
            Platform::Delete(readCallback);
            return err;
        }

        ChipLogProgress(NotSpecified, "Read request sent successfully");
        return CHIP_NO_ERROR;
    }

    // No session found on any fabric
    ChipLogError(NotSpecified, "No existing session found to node 0x" ChipLogFormatX64 " on any fabric",
                 ChipLogValueX64(targetNodeId));
    return CHIP_ERROR_NOT_CONNECTED;
}

CHIP_ERROR ExternalAttributeClient::SubscribeToAttribute(NodeId targetNodeId, EndpointId endpoint, ClusterId cluster,
                                                          AttributeId attribute, uint16_t minInterval, uint16_t maxInterval)
{
    ChipLogProgress(NotSpecified, "Creating subscription to node 0x" ChipLogFormatX64 " EP=%u CL=0x%lx ATTR=0x%lx",
                    ChipLogValueX64(targetNodeId), endpoint, static_cast<unsigned long>(cluster),
                    static_cast<unsigned long>(attribute));

    // Get the server instance
    auto & server = Server::GetInstance();

    // Find which fabric the target node is on by checking for existing sessions
    // The bridge may be on multiple fabrics (e.g., chip-tool and EFD)
    FabricIndex fabricIndex = kUndefinedFabricIndex;
    SessionManager * sessionMgr = server.GetExchangeManager().GetSessionManager();
    
    for (const auto & fabric : server.GetFabricTable())
    {
        ScopedNodeId candidatePeer(targetNodeId, fabric.GetFabricIndex());
        Optional<SessionHandle> session = sessionMgr->FindSecureSessionForNode(candidatePeer);
        
        if (session.HasValue())
        {
            fabricIndex = fabric.GetFabricIndex();
            ChipLogProgress(NotSpecified, "Found existing session to node on fabric %u", fabricIndex);
            break;
        }
    }

    if (fabricIndex == kUndefinedFabricIndex)
    {
        ChipLogError(NotSpecified, "No existing session found to node 0x" ChipLogFormatX64 " on any fabric",
                     ChipLogValueX64(targetNodeId));
        return CHIP_ERROR_NOT_CONNECTED;
    }

    // Get the exchange manager
    Messaging::ExchangeManager * exchangeMgr = &server.GetExchangeManager();

    // Create subscription context to track this subscription
    auto * context = Platform::New<SubscriptionContext>(targetNodeId, endpoint, cluster, attribute, this);
    if (context == nullptr)
    {
        return CHIP_ERROR_NO_MEMORY;
    }

    // Create the subscription callback
    auto * subscriptionCallback = Platform::New<SubscriptionCallback>(context);
    if (subscriptionCallback == nullptr)
    {
        Platform::Delete(context);
        return CHIP_ERROR_NO_MEMORY;
    }

    // Create ReadClient for subscription
    auto * readClient = Platform::New<app::ReadClient>(app::InteractionModelEngine::GetInstance(), exchangeMgr,
                                                       *subscriptionCallback, app::ReadClient::InteractionType::Subscribe);
    if (readClient == nullptr)
    {
        Platform::Delete(subscriptionCallback);
        Platform::Delete(context);
        return CHIP_ERROR_NO_MEMORY;
    }

    // Prepare subscription parameters (use default constructor - no session yet)
    app::ReadPrepareParams params;
    params.mMinIntervalFloorSeconds   = minInterval;
    params.mMaxIntervalCeilingSeconds = maxInterval;
    params.mKeepSubscriptions         = true;
    params.mIsFabricFiltered          = true;

    // Create attribute path - supports wildcards
    auto * attributePath = Platform::New<app::AttributePathParams>();
    if (attributePath == nullptr)
    {
        Platform::Delete(readClient);
        Platform::Delete(subscriptionCallback);
        Platform::Delete(context);
        return CHIP_ERROR_NO_MEMORY;
    }

    attributePath->mEndpointId  = endpoint;
    attributePath->mClusterId   = cluster;
    attributePath->mAttributeId = attribute;

    params.mpAttributePathParamsList    = attributePath;
    params.mAttributePathParamsListSize = 1;

    // Create the scoped node ID for the target
    ScopedNodeId targetPeer(targetNodeId, fabricIndex);

    ChipLogProgress(NotSpecified, "Sending subscription request to node 0x" ChipLogFormatX64 " on fabric %u",
                    ChipLogValueX64(targetNodeId), fabricIndex);

    // Use SendAutoResubscribeRequest with ScopedNodeId - this handles CASE session establishment automatically!
    CHIP_ERROR err = readClient->SendAutoResubscribeRequest(targetPeer, std::move(params));

    if (err != CHIP_NO_ERROR)
    {
        ChipLogError(NotSpecified, "Failed to send subscription request: %s", ErrorStr(err));
        Platform::Delete(readClient);
        Platform::Delete(subscriptionCallback);
        Platform::Delete(context);
        return err;
    }

    // Add to active subscriptions list
    mActiveSubscriptions.push_back(context);

    ChipLogProgress(NotSpecified, "Subscription request sent successfully");

    return CHIP_NO_ERROR;
}

void ExternalAttributeClient::ListSubscriptions()
{
    if (mActiveSubscriptions.empty())
    {
        ChipLogProgress(NotSpecified, "No active subscriptions");
        return;
    }

    ChipLogProgress(NotSpecified, "Active Subscriptions (%zu):", mActiveSubscriptions.size());
    for (const auto * sub : mActiveSubscriptions)
    {
        ChipLogProgress(NotSpecified, "  [0x%08lx] Node=0x" ChipLogFormatX64 " EP=%u CL=0x%lx ATTR=0x%lx",
                        static_cast<unsigned long>(sub->subscriptionId), ChipLogValueX64(sub->targetNodeId), sub->endpoint,
                        static_cast<unsigned long>(sub->cluster), static_cast<unsigned long>(sub->attribute));
    }
}

void ExternalAttributeClient::CancelAllSubscriptions()
{
    ChipLogProgress(NotSpecified, "Canceling all subscriptions...");

    // Clean up all subscription contexts
    for (auto * sub : mActiveSubscriptions)
    {
        Platform::Delete(sub);
    }
    mActiveSubscriptions.clear();

    // Note: The actual ReadClient objects are managed by the SDK and will be
    // cleaned up through their OnDone callbacks
}

void ExternalAttributeClient::RemoveSubscription(SubscriptionId subId)
{
    auto it = std::find_if(mActiveSubscriptions.begin(), mActiveSubscriptions.end(),
                           [subId](const SubscriptionContext * sub) { return sub->subscriptionId == subId; });

    if (it != mActiveSubscriptions.end())
    {
        Platform::Delete(*it);
        mActiveSubscriptions.erase(it);
        ChipLogProgress(NotSpecified, "Removed subscription 0x%08lx from tracking", static_cast<unsigned long>(subId));
    }
}

} // namespace bridge
} // namespace chip
