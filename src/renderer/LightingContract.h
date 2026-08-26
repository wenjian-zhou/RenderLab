#pragma once

#include "shaders/lighting_cb.h"
#include "shaders/lighting_debug_cb.h"
#include "shaders/renderer_cb.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include <nvrhi/nvrhi.h>

namespace renderlab
{
    inline constexpr uint32_t kDirectionalLightDataByteSize = 32;
    inline constexpr uint32_t kPointLightDataByteSize = 32;
    inline constexpr uint32_t kLightingConstantsByteSize = 320;
    inline constexpr uint32_t kLightingDebugConstantsByteSize = 16;

    static_assert(sizeof(DirectionalLightData) == kDirectionalLightDataByteSize);
    static_assert(sizeof(PointLightData) == kPointLightDataByteSize);
    static_assert(sizeof(LightingConstants) == kLightingConstantsByteSize);
    static_assert(sizeof(LightingDebugConstants) == kLightingDebugConstantsByteSize);
    static_assert(kLightingConstantsByteSize % 16 == 0);
    static_assert(kMaxPointLights == 8);

    inline constexpr uint32_t kLightingOffsetDirectionalToLight = 0;
    inline constexpr uint32_t kLightingOffsetDirectionalIntensity = 12;
    inline constexpr uint32_t kLightingOffsetDirectionalColor = 16;
    inline constexpr uint32_t kLightingOffsetAmbientRadiance = 32;
    inline constexpr uint32_t kLightingOffsetPointLightCount = 44;
    inline constexpr uint32_t kLightingOffsetBackgroundRadiance = 48;
    inline constexpr uint32_t kLightingOffsetFlags = 60;
    inline constexpr uint32_t kLightingOffsetPointLights = 64;

    inline constexpr const char* kHDRSceneColorDebugName = "HDRSceneColor";
    inline constexpr nvrhi::Format kHDRSceneColorFormat = nvrhi::Format::RGBA16_FLOAT;
    inline constexpr uint32_t kHDRSceneColorBytesPerPixel = 8;
    inline const nvrhi::Color kHDRSceneColorClear = nvrhi::Color(0.f, 0.f, 0.f, 1.f);

    inline constexpr float kDielectricF0 = 0.04f;
    inline constexpr float kMinGGXAlpha = 1e-3f;
    inline constexpr float kPointLightMinDistanceSq = 1e-4f;
    inline constexpr float kSpecularDenomEpsilon = 1e-5f;
    inline constexpr float kDefaultDirectionalIntensity = 4.f;
    inline constexpr donut::math::float3 kDefaultDirectionalToLightUnnormalized = {0.45f, 0.80f, 0.40f};

    inline constexpr const char* kLightingDebugWorldPositionCli = "world-position";
    inline constexpr const char* kLightingDebugNdotLCli = "ndotl";

    inline bool IsBackgroundDeviceDepth(float deviceDepth)
    {
        return deviceDepth <= 0.f;
    }

    inline donut::math::float3 ReconstructWorldPosition(
        donut::math::float2 pixelPosition,
        float deviceDepth,
        const ViewConstants& view)
    {
        const donut::math::float2 uv = (pixelPosition - view.viewportOrigin) * view.viewportSizeInv;
        const donut::math::float4 clipPos(uv.x * 2.f - 1.f, 1.f - uv.y * 2.f, deviceDepth, 1.f);
        const donut::math::float4 worldPosH = clipPos * view.matClipToWorld;
        return donut::math::float3(worldPosH.x, worldPosH.y, worldPosH.z) / worldPosH.w;
    }

    inline donut::math::float3 ComputeDiffuseAlbedo(donut::math::float3 baseColor, float metallic)
    {
        return baseColor * (1.f - metallic);
    }

    inline donut::math::float3 ComputeF0(donut::math::float3 baseColor, float metallic)
    {
        return donut::math::lerp(donut::math::float3(kDielectricF0), baseColor, metallic);
    }

    inline float PerceptualRoughnessToAlpha(float roughness)
    {
        return std::max(roughness * roughness, kMinGGXAlpha);
    }

    inline float PointLightAttenuation(float distance, float range)
    {
        if (range <= 0.f || distance >= range)
        {
            return 0.f;
        }
        return 1.f / std::max(distance * distance, kPointLightMinDistanceSq);
    }

    inline uint32_t ClampPointLightCount(uint32_t count)
    {
        return std::min(count, kMaxPointLights);
    }

    inline LightingConstants MakeDefaultLightingConstants()
    {
        LightingConstants lighting = {};
        lighting.directional.toLight = donut::math::normalize(kDefaultDirectionalToLightUnnormalized);
        lighting.directional.intensity = kDefaultDirectionalIntensity;
        lighting.directional.color = donut::math::float3(1.f);
        lighting.ambientRadiance = donut::math::float3(0.f);
        lighting.backgroundRadiance = donut::math::float3(0.f);
        lighting.flags = kLightingFlagDirectionalEnabled;
        lighting.pointLightCount = 0;
        return lighting;
    }

    inline nvrhi::TextureDesc MakeHDRSceneColorTextureDesc(uint32_t width, uint32_t height)
    {
        nvrhi::TextureDesc desc;
        desc.width = width;
        desc.height = height;
        desc.depth = 1;
        desc.arraySize = 1;
        desc.mipLevels = 1;
        desc.sampleCount = 1;
        desc.sampleQuality = 0;
        desc.format = kHDRSceneColorFormat;
        desc.dimension = nvrhi::TextureDimension::Texture2D;
        desc.debugName = kHDRSceneColorDebugName;
        desc.isShaderResource = true;
        desc.isRenderTarget = true;
        desc.isUAV = false;
        desc.isTypeless = false;
        desc.clearValue = kHDRSceneColorClear;
        desc.useClearValue = true;
        desc.initialState = nvrhi::ResourceStates::RenderTarget;
        desc.keepInitialState = true;
        return desc;
    }

    inline uint64_t EstimateHDRSceneColorBytes(uint32_t width, uint32_t height)
    {
        return static_cast<uint64_t>(width) * static_cast<uint64_t>(height) *
               static_cast<uint64_t>(kHDRSceneColorBytesPerPixel);
    }

    inline bool TextureDescMatchesHDRSceneColorContract(const nvrhi::TextureDesc& desc, std::string& error)
    {
        error.clear();
        const auto append = [&error](const char* text) {
            if (!error.empty())
            {
                error += "; ";
            }
            error += text;
        };

        if (desc.debugName != kHDRSceneColorDebugName)
        {
            append("debug name mismatch");
        }
        if (desc.format != kHDRSceneColorFormat)
        {
            append("format mismatch");
        }
        if (desc.isTypeless)
        {
            append("must be typed");
        }
        if (!desc.isRenderTarget || !desc.isShaderResource || desc.isUAV)
        {
            append("usage must be RT+SRV, no UAV");
        }
        if (!desc.useClearValue || desc.clearValue != kHDRSceneColorClear)
        {
            append("clear value mismatch");
        }
        if (desc.initialState != nvrhi::ResourceStates::RenderTarget || !desc.keepInitialState)
        {
            append("initial state mismatch");
        }
        if (desc.dimension != nvrhi::TextureDimension::Texture2D)
        {
            append("dimension must be Texture2D");
        }
        if (desc.depth != 1)
        {
            append("depth must be 1");
        }
        if (desc.arraySize != 1)
        {
            append("arraySize must be 1");
        }
        if (desc.sampleQuality != 0)
        {
            append("sampleQuality must be 0");
        }
        if (desc.sampleCount != 1 || desc.mipLevels != 1)
        {
            append("must be 1x MSAA with one mip");
        }
        // Width and height are not format-contract fields. Zero size is rejected
        // at Create time, matching GBufferTargets, not by this matcher or the factory.
        return error.empty();
    }
}
