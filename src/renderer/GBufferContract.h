#pragma once

#include <cstdint>

#include <nvrhi/nvrhi.h>

namespace renderlab
{
    inline constexpr uint32_t kGBufferFlagShadingValid = 1u << 0;
    inline constexpr uint32_t kGBufferFlagTwoSided     = 1u << 1;
    inline constexpr uint32_t kGBufferFlagAlphaTested  = 1u << 2;

    enum class GBufferTarget : uint32_t
    {
        A = 0,
        B = 1,
        C = 2,
        Depth = 3,
        Count = 4
    };

    struct GBufferFormatDesc
    {
        const char* debugName;
        nvrhi::Format format;
        bool typeless;
        nvrhi::Color clearColor;
    };

    inline const GBufferFormatDesc kGBufferFormats[] = {
        { "GBufferA",     nvrhi::Format::SRGBA8_UNORM, false, nvrhi::Color(0.f, 0.f, 0.f, 1.f) },
        { "GBufferB",     nvrhi::Format::RGBA16_FLOAT, false, nvrhi::Color(0.f) },
        { "GBufferC",     nvrhi::Format::RGBA8_UNORM,  false, nvrhi::Color(0.f, 0.f, 0.f, 1.f) },
        { "GBufferDepth", nvrhi::Format::D32,          true,  nvrhi::Color(0.f) },
    };

    inline float PackGBufferFlags(uint32_t flags)
    {
        return static_cast<float>(flags) / 255.0f;
    }

    inline uint32_t UnpackGBufferFlags(float packed)
    {
        return static_cast<uint32_t>(packed * 255.0f + 0.5f);
    }
}
