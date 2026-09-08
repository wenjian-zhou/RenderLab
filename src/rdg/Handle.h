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

    // Logical handles are plain values with public fields: {index, version,
    // graphId}, no bit packing. The distinct handle types name the resource
    // kind at compile time for honest use; the registry's kind tag and
    // version check are the runtime safety net for forged or stale values
    // (docs/rdg.md). version is the S4.2 logical-version slot (a write will
    // return a new handle; S4.1 mints every handle at 0), and graphId, from
    // a global counter, makes cross-graph use a deterministic failure.
    struct TextureHandle
    {
        uint32_t index = kNullHandleIndex;
        uint32_t version = 0;
        uint32_t graphId = 0;

        bool isNull() const { return index == kNullHandleIndex; }

        friend bool operator==(const TextureHandle&, const TextureHandle&) = default;
    };

    struct BufferHandle
    {
        uint32_t index = kNullHandleIndex;
        uint32_t version = 0;
        uint32_t graphId = 0;

        bool isNull() const { return index == kNullHandleIndex; }

        friend bool operator==(const BufferHandle&, const BufferHandle&) = default;
    };
}
