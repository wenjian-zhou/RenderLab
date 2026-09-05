#include "renderer/LightingContract.h"
#include "renderer/RendererData.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace donut::math;
using namespace renderlab;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* message)
    {
        if (condition)
        {
            std::printf("  PASS  %s\n", message);
            std::fflush(stdout);
            return;
        }
        std::printf("  FAIL  %s\n", message);
        std::fflush(stdout);
        ++g_failures;
    }

    bool Near(float actual, float expected, float tol)
    {
        return std::fabs(actual - expected) <= tol;
    }

    bool Near(const float3& actual, const float3& expected, float tol)
    {
        return Near(actual.x, expected.x, tol) &&
               Near(actual.y, expected.y, tol) &&
               Near(actual.z, expected.z, tol);
    }

    template <typename T, typename M>
    uint32_t FieldOffset(const T* object, const M* member)
    {
        return static_cast<uint32_t>(
            reinterpret_cast<const char*>(member) - reinterpret_cast<const char*>(object));
    }
}

int RunLightingContractTests()
{
    std::printf("RenderLab S2.1 lighting contract tests\n");
    std::fflush(stdout);

    LightingConstants lighting = {};
    Check(sizeof(DirectionalLightData) == 32, "DirectionalLightData is 32 bytes");
    Check(sizeof(PointLightData) == 32, "PointLightData is 32 bytes");
    Check(sizeof(LightingConstants) == 320, "LightingConstants is 320 bytes");
    Check(sizeof(LightingDebugConstants) == 16, "LightingDebugConstants is 16 bytes");
    Check(kMaxPointLights == 8, "Maximum point-light count is 8");

    Check(FieldOffset(&lighting, &lighting.directional.toLight) == kLightingOffsetDirectionalToLight,
          "directional.toLight @ 0");
    Check(FieldOffset(&lighting, &lighting.directional.intensity) == kLightingOffsetDirectionalIntensity,
          "directional.intensity @ 12");
    Check(FieldOffset(&lighting, &lighting.directional.color) == kLightingOffsetDirectionalColor,
          "directional.color @ 16");
    Check(FieldOffset(&lighting, &lighting.ambientRadiance) == kLightingOffsetAmbientRadiance,
          "ambientRadiance @ 32");
    Check(FieldOffset(&lighting, &lighting.pointLightCount) == kLightingOffsetPointLightCount,
          "pointLightCount @ 44");
    Check(FieldOffset(&lighting, &lighting.backgroundRadiance) == kLightingOffsetBackgroundRadiance,
          "backgroundRadiance @ 48");
    Check(FieldOffset(&lighting, &lighting.flags) == kLightingOffsetFlags, "flags @ 60");
    Check(FieldOffset(&lighting, &lighting.pointLights) == kLightingOffsetPointLights, "pointLights @ 64");

    Check(Near(ComputeF0(float3(0.8f, 0.2f, 0.1f), 0.f), float3(0.04f), 1e-6f),
          "Dielectric F0 is 0.04");
    Check(Near(ComputeF0(float3(0.8f, 0.2f, 0.1f), 1.f), float3(0.8f, 0.2f, 0.1f), 1e-6f),
          "Metal F0 is base color");
    Check(Near(ComputeF0(float3(0.8f, 0.2f, 0.1f), 0.5f), float3(0.42f, 0.12f, 0.07f), 1e-5f),
          "F0 at metallic 0.5 is the lerp");
    Check(Near(ComputeDiffuseAlbedo(float3(0.8f, 0.2f, 0.1f), 1.f), float3(0.f), 1e-6f),
          "Metal has no diffuse albedo");
    Check(Near(ComputeDiffuseAlbedo(float3(0.8f, 0.2f, 0.1f), 0.f), float3(0.8f, 0.2f, 0.1f), 1e-6f),
          "Dielectric diffuse albedo is base color");

    Check(Near(PerceptualRoughnessToAlpha(0.f), kMinGGXAlpha, 1e-8f), "Roughness 0 floors alpha to 1e-3");
    Check(Near(PerceptualRoughnessToAlpha(1.f), 1.f, 1e-6f), "Roughness 1 -> alpha 1");
    Check(Near(PerceptualRoughnessToAlpha(0.5f), 0.25f, 1e-6f), "Roughness 0.5 -> alpha 0.25");

    Check(Near(PointLightAttenuation(0.f, 10.f), 1.f / kPointLightMinDistanceSq, 1e-3f),
          "d = 0 is finite");
    Check(Near(PointLightAttenuation(2.f, 10.f), 0.25f, 1e-6f), "d = 2 -> 1/4");
    Check(Near(PointLightAttenuation(2.f, 100.f), 0.25f, 1e-6f),
          "Same d and different ranges match inside both ranges");
    Check(PointLightAttenuation(10.f, 10.f) == 0.f, "d == range is 0");
    Check(PointLightAttenuation(11.f, 10.f) == 0.f, "d > range is 0");
    Check(PointLightAttenuation(2.f, 0.f) == 0.f, "range <= 0 is 0");

    Check(ClampPointLightCount(0) == 0, "Count 0 stays 0");
    Check(ClampPointLightCount(8) == 8, "Count 8 stays 8");
    Check(ClampPointLightCount(9) == 8, "Count 9 clamps to 8");

    Check(IsBackgroundDeviceDepth(0.f), "deviceDepth 0 is background");
    Check(IsBackgroundDeviceDepth(-0.1f), "deviceDepth < 0 is background");
    Check(!IsBackgroundDeviceDepth(1.f), "Near-plane depth is not background");

    const LightingConstants defaults = MakeDefaultLightingConstants();
    Check(defaults.pointLightCount == 0, "Default point-light count is 0");
    Check((defaults.flags & kLightingFlagDirectionalEnabled) != 0, "Default directional is enabled");
    Check(Near(defaults.ambientRadiance, float3(0.f), 1e-6f), "Default ambient is 0");
    Check(Near(defaults.backgroundRadiance, float3(0.f), 1e-6f), "Default background radiance is 0");
    Check(Near(defaults.directional.intensity, kDefaultDirectionalIntensity, 1e-6f),
          "Default directional intensity is 4");
    Check(Near(length(defaults.directional.toLight), 1.f, 1e-5f), "Default toLight is unit length");

    Check(LightingDebugMode_WorldPosition == 0 && LightingDebugMode_NdotL == 1 &&
              LightingDebugMode_Lit == 2,
          "S2.3 debug mode values are frozen");
    Check(std::string(kLightingDebugWorldPositionCli) == "world-position", "world-position CLI name");
    Check(std::string(kLightingDebugNdotLCli) == "ndotl", "ndotl CLI name");
    Check(std::string(kLightingDebugLitCli) == "lit", "lit CLI name");

    const LightingConstants verify = MakeVerifyLightsLightingConstants();
    Check(verify.pointLightCount == 1, "Verify-lights point count is 1");
    Check(Near(verify.ambientRadiance, kVerifyLightsAmbientRadiance, 1e-6f),
          "Verify-lights ambient matches the fixture");
    Check(Near(verify.pointLights[0].position, kVerifyLightsPointPosition, 1e-6f),
          "Verify-lights point position matches the fixture");

    const nvrhi::TextureDesc hdr = MakeHDRSceneColorTextureDesc(1280, 720);
    std::string hdrError;
    Check(TextureDescMatchesHDRSceneColorContract(hdr, hdrError), "HDRSceneColor descriptor matches the contract");
    if (!hdrError.empty())
    {
        std::printf("    %s\n", hdrError.c_str());
    }
    Check(hdr.width == 1280 && hdr.height == 720, "HDR desc size matches the requested back buffer");
    Check(hdr.dimension == nvrhi::TextureDimension::Texture2D, "HDR desc is Texture2D");
    Check(hdr.depth == 1 && hdr.arraySize == 1 && hdr.sampleQuality == 0,
          "HDR desc depth, arraySize, and sampleQuality match the contract");
    Check(EstimateHDRSceneColorBytes(1280, 720) == 7372800ull, "1280x720 HDR allocation is 7,372,800 bytes");

    std::string zeroSizeError;
    Check(
        TextureDescMatchesHDRSceneColorContract(MakeHDRSceneColorTextureDesc(0, 0), zeroSizeError),
        "Zero width/height is not a descriptor-contract mismatch");

    auto rejectMutated = [&](nvrhi::TextureDesc mutated, const char* message) {
        std::string mismatch;
        Check(!TextureDescMatchesHDRSceneColorContract(mutated, mismatch), message);
        Check(!mismatch.empty(), "HDR contract mismatch produces an error string");
    };

    nvrhi::TextureDesc mutated = hdr;
    mutated.dimension = nvrhi::TextureDimension::Texture3D;
    rejectMutated(mutated, "Non-2D HDR dimension is rejected");
    mutated = hdr;
    mutated.depth = 2;
    rejectMutated(mutated, "HDR depth != 1 is rejected");
    mutated = hdr;
    mutated.arraySize = 2;
    rejectMutated(mutated, "HDR arraySize != 1 is rejected");
    mutated = hdr;
    mutated.sampleQuality = 1;
    rejectMutated(mutated, "HDR sampleQuality != 0 is rejected");

    ViewFillDesc viewDesc;
    viewDesc.worldToView = MakeFirstPersonWorldToView(float3(4.8f, 2.4f, 5.6f), float3(0.f, 0.85f, 0.f), float3(0.f, 1.f, 0.f));
    viewDesc.cameraPosition = float3(4.8f, 2.4f, 5.6f);
    viewDesc.verticalFovDegrees = 45.f;
    viewDesc.zNear = 0.1f;
    viewDesc.viewportWidth = 1280.f;
    viewDesc.viewportHeight = 720.f;
    const ViewConstants view = MakeViewConstants(viewDesc);

    const float3 world(1.f, 2.f, 3.f);
    const float4 clip = float4(world, 1.f) * view.matWorldToClip;
    const float3 ndc = float3(clip.x, clip.y, clip.z) / clip.w;
    Check(ndc.z > 0.f, "S0.4 camera sees the test point with reversed-Z depth > 0");

    const float2 uv(ndc.x * 0.5f + 0.5f, 0.5f - ndc.y * 0.5f);
    const float2 pixel = view.viewportOrigin + uv * view.viewportSize;
    const float3 recovered = ReconstructWorldPosition(pixel, ndc.z, view);
    Check(Near(recovered, world, 1e-3f), "NDC device depth reconstructs the S0.4 world point");

    return g_failures;
}
