#pragma once

#include <cstdint>

namespace renderlab::rdg
{
    inline constexpr uint32_t kNullHandleIndex = 0xFFFFFFFFu;

    enum class ResourceKind : uint8_t
    {
        Texture,
        Buffer,
    };

    struct TextureHandle
    {
        uint32_t index = kNullHandleIndex;
        uint32_t version = 0;
        uint32_t graphId = 0;

        bool IsNull() const { return index == kNullHandleIndex; }

        friend bool operator==(const TextureHandle&, const TextureHandle&) = default;
    };

    struct BufferHandle
    {
        uint32_t index = kNullHandleIndex;
        uint32_t version = 0;
        uint32_t graphId = 0;

        bool IsNull() const { return index == kNullHandleIndex; }

        friend bool operator==(const BufferHandle&, const BufferHandle&) = default;
    };
}
