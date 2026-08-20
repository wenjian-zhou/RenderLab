#include "renderer/GBufferDebugPass.h"
#include "renderer/GBufferContract.h"
#include "shaders/gbuffer_debug_cb.h"
#include "shaders/renderer_cb.h"

#include <cmath>
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

    bool Near(float actual, float expected, float tol)
    {
        return std::fabs(actual - expected) <= tol;
    }
}

int RunGBufferDebugPassTests()
{
    std::printf("RenderLab S1.5 GBuffer debug visualization tests\n");

    Check(static_cast<uint32_t>(GBufferDebugMode::Count) == 6u, "ADR-002 requires exactly six debug modes");
    Check(GBufferDebugMode_BaseColor == 0u, "Base color is mode 0");
    Check(GBufferDebugMode_WorldNormal == 1u, "World normal is mode 1");
    Check(GBufferDebugMode_Roughness == 2u, "Roughness is mode 2");
    Check(GBufferDebugMode_Metallic == 3u, "Metallic is mode 3");
    Check(GBufferDebugMode_AoFlags == 4u, "AO/flags is mode 4");
    Check(GBufferDebugMode_LinearDepth == 5u, "Linearized depth is mode 5");

    Check(GetGBufferDebugModeInfo(GBufferDebugMode::BaseColor).channelName == std::string("Base color"),
          "Base color channel name");
    Check(GetGBufferDebugModeInfo(GBufferDebugMode::WorldNormal).channelName == std::string("World normal"),
          "World normal channel name");
    Check(GetGBufferDebugModeInfo(GBufferDebugMode::Roughness).channelName == std::string("Roughness"),
          "Roughness channel name");
    Check(GetGBufferDebugModeInfo(GBufferDebugMode::Metallic).channelName == std::string("Metallic"),
          "Metallic channel name");
    Check(GetGBufferDebugModeInfo(GBufferDebugMode::AoFlags).channelName == std::string("AO / material flags"),
          "AO/flags channel name");
    Check(GetGBufferDebugModeInfo(GBufferDebugMode::LinearDepth).channelName == std::string("Linearized depth"),
          "Linearized depth channel name");

    const char* normalConvention = GetGBufferDebugModeInfo(GBufferDebugMode::WorldNormal).decodeConvention;
    Check(std::string(normalConvention).find("0.5 * n + 0.5") != std::string::npos,
          "Normal decode convention documents the display remap");
    const char* depthConvention = GetGBufferDebugModeInfo(GBufferDebugMode::LinearDepth).decodeConvention;
    Check(std::string(depthConvention).find("zNear / deviceDepth") != std::string::npos,
          "Depth decode convention documents viewZ = zNear / deviceDepth");
    Check(std::string(depthConvention).find("deviceDepth == 0") != std::string::npos,
          "Depth decode convention documents rejection of cleared depth");

    GBufferDebugMode parsed = GBufferDebugMode::Count;
    std::string parseError;
    Check(ParseGBufferDebugMode("base-color", parsed, parseError) && parsed == GBufferDebugMode::BaseColor,
          "Parse base-color");
    Check(ParseGBufferDebugMode("normals", parsed, parseError) && parsed == GBufferDebugMode::WorldNormal,
          "Parse normals alias");
    Check(ParseGBufferDebugMode("roughness", parsed, parseError) && parsed == GBufferDebugMode::Roughness,
          "Parse roughness");
    Check(ParseGBufferDebugMode("metallic", parsed, parseError) && parsed == GBufferDebugMode::Metallic,
          "Parse metallic");
    Check(ParseGBufferDebugMode("ao-flags", parsed, parseError) && parsed == GBufferDebugMode::AoFlags,
          "Parse ao-flags");
    Check(ParseGBufferDebugMode("linear-depth", parsed, parseError) && parsed == GBufferDebugMode::LinearDepth,
          "Parse linear-depth");
    Check(!ParseGBufferDebugMode("lighting", parsed, parseError), "Unknown mode is rejected");

    const nvrhi::RasterState raster = MakeGBufferDebugRasterState();
    Check(raster.cullMode == nvrhi::RasterCullMode::None, "Debug pass culls none");
    const nvrhi::DepthStencilState depth = MakeGBufferDebugDepthState();
    Check(!depth.depthTestEnable, "Debug pass does not depth-test");
    Check(!depth.depthWriteEnable, "Debug pass does not write depth");

    float viewZ = 0.f;
    Check(!LinearizeViewDepth(0.f, 0.1f, viewZ), "deviceDepth == 0 is rejected");
    Check(LinearizeViewDepth(1.f, 0.1f, viewZ) && Near(viewZ, 0.1f, 1e-6f), "Near plane deviceDepth 1 -> viewZ = zNear");
    Check(LinearizeViewDepth(0.5f, 0.1f, viewZ) && Near(viewZ, 0.2f, 1e-6f), "deviceDepth 0.5 -> viewZ = 0.2");

    float nearZ = 0.f;
    float midZ = 0.f;
    float farZ = 0.f;
    Check(LinearizeViewDepth(1.f, 0.1f, nearZ), "Near linearize succeeds");
    Check(LinearizeViewDepth(0.5f, 0.1f, midZ), "Mid linearize succeeds");
    Check(LinearizeViewDepth(0.1f, 0.1f, farZ), "Far linearize succeeds");
    Check(nearZ < midZ && midZ < farZ, "Reversed-Z linearized viewZ is monotonic (smaller deviceDepth -> larger viewZ)");
    Check(std::isfinite(nearZ) && std::isfinite(midZ) && std::isfinite(farZ), "Linearized viewZ is finite");

    const float displayNear = LinearDepthDisplayValue(nearZ);
    const float displayMid = LinearDepthDisplayValue(midZ);
    const float displayFar = LinearDepthDisplayValue(farZ);
    Check(displayNear < displayMid && displayMid < displayFar, "Displayed viewZ/(viewZ+1) stays monotonic");
    Check(displayNear > 0.f && displayFar < 1.f, "Displayed linearized depth stays in (0, 1)");

    Check(kGBufferFormats[3].debugName == std::string("GBufferDepth"), "Debug pass reads the frozen GBufferDepth resource");
    Check(sizeof(GBufferDebugConstants) == 16, "GBufferDebugConstants is 16 bytes");

    GBufferDebugPassOutputs outputs;
    Check(outputs.debugColor == nullptr, "Debug color starts unbound");

    return g_failures;
}
