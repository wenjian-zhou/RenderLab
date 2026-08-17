#pragma once

#include <d3d12.h>
#include <windows.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace renderlab::gfx::d3d12
{
struct InfoQueueMessageData
{
    D3D12_MESSAGE_CATEGORY category =
        D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED;
    D3D12_MESSAGE_SEVERITY severity =
        D3D12_MESSAGE_SEVERITY_MESSAGE;
    D3D12_MESSAGE_ID id = D3D12_MESSAGE_ID_UNKNOWN;
    std::string description;
};

struct InfoQueueCollectionResult
{
    HRESULT status = S_OK;
    bool infoQueueAvailable = false;
    std::uint64_t storedMessageCount = 0;
    bool collectionLimitExceeded = false;
    bool storedMessagesCleared = false;

    std::uint32_t corruptionCount = 0;
    std::uint32_t errorCount = 0;
    std::uint32_t warningCount = 0;
    std::uint32_t informationCount = 0;
    std::uint32_t messageCount = 0;
    std::uint32_t unknownSeverityCount = 0;
    bool hasRunFailure = false;

    std::vector<InfoQueueMessageData> messages;
};

void ClassifyInfoQueueMessages(
    std::span<const InfoQueueMessageData> messages,
    InfoQueueCollectionResult &result) noexcept;

[[nodiscard]] InfoQueueCollectionResult CollectInfoQueueMessages(
    ID3D12InfoQueue *infoQueue);
} // namespace renderlab::gfx::d3d12
