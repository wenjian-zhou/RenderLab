#include "renderer/GBufferPass.h"
#include "renderer/GBufferContract.h"
#include "shaders/renderer_cb.h"

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

int RunGBufferPassContractTests()
{
    std::printf("RenderLab S1.4 GBuffer pass tests\n");

    GBufferRasterKey mirroredOpaque;
    mirroredOpaque.frontCounterClockwise = true;
    mirroredOpaque.twoSided = false;
    const nvrhi::RasterState mirrored = MakeGBufferRasterState(mirroredOpaque);
    Check(mirrored.frontCounterClockwise, "Mirrored view uses frontCounterClockwise");
    Check(mirrored.cullMode == nvrhi::RasterCullMode::Back, "Opaque single-sided culls back faces");
    Check(mirrored.depthClipEnable, "depthClipEnable is true (NVRHI default is false)");

    GBufferRasterKey twoSidedKey;
    twoSidedKey.frontCounterClockwise = true;
    twoSidedKey.twoSided = true;
    const nvrhi::RasterState twoSided = MakeGBufferRasterState(twoSidedKey);
    Check(twoSided.cullMode == nvrhi::RasterCullMode::None, "glTF doubleSided uses cull None");
    Check(twoSided.frontCounterClockwise, "Two-sided still uses the mirrored winding");

    GBufferRasterKey unmirrored;
    unmirrored.frontCounterClockwise = false;
    unmirrored.twoSided = false;
    const nvrhi::RasterState cw = MakeGBufferRasterState(unmirrored);
    Check(!cw.frontCounterClockwise, "Unmirrored view does not force frontCounterClockwise");

    const nvrhi::DepthStencilState depth = MakeGBufferDepthState();
    Check(depth.depthTestEnable, "Depth test is enabled");
    Check(depth.depthWriteEnable, "Depth write is enabled");
    Check(depth.depthFunc == nvrhi::ComparisonFunc::GreaterOrEqual, "Reversed-Z uses GreaterOrEqual");

    Check((static_cast<uint32_t>(RendererViewFlag::Mirrored) & 1u) == 1u, "View mirrored flag is bit 0 for frontCounterClockwise");
    Check(kGBufferFormats[0].debugName == std::string("GBufferA"), "Pass still writes frozen GBufferA");
    Check(kGBufferFormats[1].debugName == std::string("GBufferB"), "Pass still writes frozen GBufferB");
    Check(kGBufferFormats[2].debugName == std::string("GBufferC"), "Pass still writes frozen GBufferC");
    Check(kGBufferFormats[3].debugName == std::string("GBufferDepth"), "Pass still writes frozen GBufferDepth");

    GBufferPassOutputs outputs;
    Check(outputs.gbufferA == nullptr && outputs.gbufferDepth == nullptr, "Outputs start unbound until S1.3 targets exist");

    return g_failures;
}
