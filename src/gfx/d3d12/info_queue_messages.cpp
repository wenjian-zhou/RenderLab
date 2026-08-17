#include "gfx/d3d12/info_queue_messages.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace renderlab::gfx::d3d12
{
namespace
{
constexpr std::uint64_t maximumStoredMessages = 4096;
constexpr SIZE_T maximumMessageSize = 1024 * 1024;

bool DescriptionIsWithinStorage(
    const D3D12_MESSAGE &message,
    const void *storage,
    SIZE_T storageSize) noexcept
{
    if (message.pDescription == nullptr)
    {
        return message.DescriptionByteLength == 0;
    }

    const auto storageBegin = reinterpret_cast<std::uintptr_t>(storage);
    const auto descriptionBegin =
        reinterpret_cast<std::uintptr_t>(message.pDescription);
    if (descriptionBegin < storageBegin)
    {
        return false;
    }

    const auto descriptionOffset = descriptionBegin - storageBegin;
    return descriptionOffset <= storageSize &&
           message.DescriptionByteLength <= storageSize - descriptionOffset;
}

std::string CopyDescription(const D3D12_MESSAGE &message)
{
    if (message.pDescription == nullptr || message.DescriptionByteLength == 0)
    {
        return "unavailable";
    }

    SIZE_T descriptionLength = message.DescriptionByteLength;
    if (message.pDescription[descriptionLength - 1] == '\0')
    {
        --descriptionLength;
    }

    std::string description(
        message.pDescription,
        static_cast<std::size_t>(descriptionLength));
    std::replace(description.begin(), description.end(), '\r', ' ');
    std::replace(description.begin(), description.end(), '\n', ' ');
    return description.empty() ? "unavailable" : description;
}

void ClassifyMessage(
    const InfoQueueMessageData &message,
    InfoQueueCollectionResult &result) noexcept
{
    switch (message.severity)
    {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        ++result.corruptionCount;
        break;
    case D3D12_MESSAGE_SEVERITY_ERROR:
        ++result.errorCount;
        break;
    case D3D12_MESSAGE_SEVERITY_WARNING:
        ++result.warningCount;
        break;
    case D3D12_MESSAGE_SEVERITY_INFO:
        ++result.informationCount;
        break;
    case D3D12_MESSAGE_SEVERITY_MESSAGE:
        ++result.messageCount;
        break;
    default:
        ++result.unknownSeverityCount;
        break;
    }
}
} // namespace

void ClassifyInfoQueueMessages(
    std::span<const InfoQueueMessageData> messages,
    InfoQueueCollectionResult &result) noexcept
{
    result.corruptionCount = 0;
    result.errorCount = 0;
    result.warningCount = 0;
    result.informationCount = 0;
    result.messageCount = 0;
    result.unknownSeverityCount = 0;

    for (const auto &message : messages)
    {
        ClassifyMessage(message, result);
    }

    result.hasRunFailure =
        result.corruptionCount != 0 || result.errorCount != 0;
}

InfoQueueCollectionResult CollectInfoQueueMessages(
    ID3D12InfoQueue *infoQueue)
{
    InfoQueueCollectionResult result = {};
    if (infoQueue == nullptr)
    {
        return result;
    }

    result.infoQueueAvailable = true;
    result.storedMessageCount =
        infoQueue->GetNumStoredMessagesAllowedByRetrievalFilter();

    if (result.storedMessageCount > maximumStoredMessages)
    {
        result.status = HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW);
        result.collectionLimitExceeded = true;
        return result;
    }

    result.messages.reserve(
        static_cast<std::size_t>(result.storedMessageCount));

    for (std::uint64_t index = 0;
         index < result.storedMessageCount;
         ++index)
    {
        SIZE_T messageSize = 0;
        result.status = infoQueue->GetMessage(index, nullptr, &messageSize);
        if (FAILED(result.status))
        {
            ClassifyInfoQueueMessages(result.messages, result);
            return result;
        }

        if (messageSize < sizeof(D3D12_MESSAGE) ||
            messageSize > maximumMessageSize)
        {
            result.status = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            result.collectionLimitExceeded = messageSize > maximumMessageSize;
            ClassifyInfoQueueMessages(result.messages, result);
            return result;
        }

        const SIZE_T wordCount =
            (messageSize + sizeof(std::uint64_t) - 1) /
            sizeof(std::uint64_t);
        std::vector<std::uint64_t> storage(
            static_cast<std::size_t>(wordCount));
        auto *message = reinterpret_cast<D3D12_MESSAGE *>(storage.data());

        SIZE_T retrievedSize = messageSize;
        result.status = infoQueue->GetMessage(
            index,
            message,
            &retrievedSize);
        if (FAILED(result.status))
        {
            ClassifyInfoQueueMessages(result.messages, result);
            return result;
        }

        if (retrievedSize < sizeof(D3D12_MESSAGE) ||
            retrievedSize > messageSize ||
            !DescriptionIsWithinStorage(
                *message,
                storage.data(),
                retrievedSize))
        {
            result.status = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
            ClassifyInfoQueueMessages(result.messages, result);
            return result;
        }

        InfoQueueMessageData messageData{
            .category = message->Category,
            .severity = message->Severity,
            .id = message->ID,
            .description = CopyDescription(*message),
        };
        result.messages.push_back(std::move(messageData));
    }

    ClassifyInfoQueueMessages(result.messages, result);
    result.status = S_OK;
    infoQueue->ClearStoredMessages();
    result.storedMessagesCleared = true;
    return result;
}
} // namespace renderlab::gfx::d3d12
