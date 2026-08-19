#include "renderer/GBufferTargets.h"
#include "renderer/GBufferContract.h"

#include <cstdio>
#include <string>

using namespace renderlab;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* message)
    {
        if (condition)
        {
            std::printf("  PASS  %s\n", message);
            return;
        }
        std::printf("  FAIL  %s\n", message);
        ++g_failures;
    }
}

int RunGBufferTargetContractTests()
{
    std::printf("RenderLab S1.3 GBuffer target tests\n");

    Check(
        kGBufferFormats[static_cast<size_t>(GBufferTarget::A)].debugName == std::string("GBufferA"),
        "GBufferA debug name is frozen");
    Check(
        kGBufferFormats[static_cast<size_t>(GBufferTarget::B)].debugName == std::string("GBufferB"),
        "GBufferB debug name is frozen");
    Check(
        kGBufferFormats[static_cast<size_t>(GBufferTarget::C)].debugName == std::string("GBufferC"),
        "GBufferC debug name is frozen");
    Check(
        kGBufferFormats[static_cast<size_t>(GBufferTarget::Depth)].debugName == std::string("GBufferDepth"),
        "GBufferDepth debug name is frozen");

    Check(kGBufferFormats[0].format == nvrhi::Format::SRGBA8_UNORM, "GBufferA is SRGBA8_UNORM");
    Check(kGBufferFormats[1].format == nvrhi::Format::RGBA16_FLOAT, "GBufferB is RGBA16_FLOAT");
    Check(kGBufferFormats[2].format == nvrhi::Format::RGBA8_UNORM, "GBufferC is RGBA8_UNORM");
    Check(kGBufferFormats[3].format == nvrhi::Format::D32, "GBufferDepth is D32");
    Check(!kGBufferFormats[0].typeless && !kGBufferFormats[1].typeless && !kGBufferFormats[2].typeless,
          "Color targets are typed");
    Check(kGBufferFormats[3].typeless, "Depth is typeless");

    Check(kGBufferFormats[0].clearColor == nvrhi::Color(0.f, 0.f, 0.f, 1.f), "GBufferA clear is (0,0,0,1)");
    Check(kGBufferFormats[1].clearColor == nvrhi::Color(0.f), "GBufferB clear is (0,0,0,0)");
    Check(kGBufferFormats[2].clearColor == nvrhi::Color(0.f, 0.f, 0.f, 1.f), "GBufferC clear is (0,0,0,1)");
    Check(kGBufferFormats[3].clearColor == nvrhi::Color(0.f), "GBufferDepth clear is 0");

    Check(GBufferBytesPerPixel(GBufferTarget::A) == 4, "GBufferA is 4 bytes/pixel");
    Check(GBufferBytesPerPixel(GBufferTarget::B) == 8, "GBufferB is 8 bytes/pixel");
    Check(GBufferBytesPerPixel(GBufferTarget::C) == 4, "GBufferC is 4 bytes/pixel");
    Check(GBufferBytesPerPixel(GBufferTarget::Depth) == 4, "GBufferDepth is 4 bytes/pixel");
    Check(EstimateGBufferTotalBytes(1280, 720) == 18432000ull, "1280x720 allocation is 18,432,000 bytes");
    Check(EstimateGBufferTargetBytes(GBufferTarget::B, 1280, 720) == 7372800ull, "GBufferB at 1280x720 is 7,372,800");

    const uint32_t width = 1344;
    const uint32_t height = 784;
    for (uint32_t index = 0; index < static_cast<uint32_t>(GBufferTarget::Count); ++index)
    {
        const GBufferTarget target = static_cast<GBufferTarget>(index);
        const nvrhi::TextureDesc desc = MakeGBufferTextureDesc(target, width, height);
        std::string error;
        Check(
            TextureDescMatchesGBufferContract(desc, target, error),
            kGBufferFormats[index].debugName);
        if (!error.empty())
        {
            std::printf("    %s\n", error.c_str());
        }
        Check(desc.width == width && desc.height == height, "Desc size matches back buffer");
        Check(desc.sampleCount == 1 && desc.mipLevels == 1, "No MSAA and no mip chain");
    }

    const nvrhi::TextureDesc depth = MakeGBufferTextureDesc(GBufferTarget::Depth, 64, 64);
    Check(depth.isTypeless, "Depth desc is typeless");
    Check(depth.isRenderTarget, "Depth desc sets isRenderTarget for ALLOW_DEPTH_STENCIL");
    Check(depth.isShaderResource, "Depth desc is a shader resource");
    Check(depth.initialState == nvrhi::ResourceStates::DepthWrite, "Depth initial state is DepthWrite");
    Check(depth.keepInitialState, "Depth uses NVRHI automatic state tracking");

    const nvrhi::TextureDesc colorA = MakeGBufferTextureDesc(GBufferTarget::A, 64, 64);
    Check(colorA.initialState == nvrhi::ResourceStates::RenderTarget, "Color initial state is RenderTarget");
    Check(colorA.keepInitialState, "Color uses NVRHI automatic state tracking");
    Check(!colorA.isTypeless, "Color desc is not typeless");

    nvrhi::TextureDesc mutated = colorA;
    mutated.debugName = "WrongName";
    std::string mismatch;
    Check(!TextureDescMatchesGBufferContract(mutated, GBufferTarget::A, mismatch), "Wrong debug name is rejected");
    Check(!mismatch.empty(), "Contract mismatch produces an error string");

    GBufferTargets empty;
    Check(!empty.IsValid(), "Uncreated GBufferTargets is invalid");
    Check(empty.GetTexture(GBufferTarget::A) == nullptr, "Uncreated SRV handle is null");
    Check(empty.GetShaderResource(GBufferTarget::Depth) == nullptr, "Uncreated depth SRV handle is null");
    Check(empty.GetApproximateBytes() == 0, "Uncreated allocation size is 0");

    return g_failures;
}
