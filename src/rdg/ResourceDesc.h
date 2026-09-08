#pragma once

#include <cstdint>
#include <string>

namespace renderlab::rdg
{
    // Neutral logical format: Stage 4 has no NVRHI dependency, and S5.2 owns
    // the mapping table from these values to nvrhi::FormatType. The set
    // covers the frozen M1 pipeline resources (docs/m1-reference.md §4);
    // extend it only when a real resource needs a new value.
    enum class Format : uint8_t
    {
        Unknown,
        Srgba8Unorm,
        Rgba8Unorm,
        Rgba16Float,
        Rgba32Float,
        R32Float,
        D32Float,
    };

    // Descriptors are value types; equality is the future pooling key
    // (S5.7; UE precedent: FRDGBufferDesc::operator==).
    struct TextureDesc
    {
        std::string name;
        uint32_t width = 0;
        uint32_t height = 0;
        Format format = Format::Unknown;

        friend bool operator==(const TextureDesc&, const TextureDesc&) = default;
    };

    struct BufferDesc
    {
        std::string name;
        uint32_t bytesPerElement = 0;
        uint32_t numElements = 0;

        friend bool operator==(const BufferDesc&, const BufferDesc&) = default;
    };
}
