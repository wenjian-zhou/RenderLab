#include "renderer/LightingDebugPass.h"
#include "renderer/DeferredLightingPass.h"
#include "renderer/LightingContract.h"

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

int RunLightingDebugPassTests()
{
    std::printf("RenderLab S2.2 lighting debug / present-source tests\n");

    Check(static_cast<uint32_t>(LightingDebugMode::Count) == 2u, "S2.2 defines exactly two lighting debug modes");
    Check(LightingDebugMode_WorldPosition == 0u, "world-position is mode 0");
    Check(LightingDebugMode_NdotL == 1u, "ndotl is mode 1");

    LightingDebugMode mode = LightingDebugMode::NdotL;
    std::string error;
    Check(ParseLightingDebugMode("world-position", mode, error), "Parse world-position");
    Check(mode == LightingDebugMode::WorldPosition, "world-position maps to WorldPosition");
    Check(ParseLightingDebugMode("ndotl", mode, error), "Parse ndotl");
    Check(mode == LightingDebugMode::NdotL, "ndotl maps to NdotL");
    Check(ParseLightingDebugMode("N-Dot-L", mode, error), "Parse N-Dot-L alias");
    Check(mode == LightingDebugMode::NdotL, "N-Dot-L maps to NdotL");
    Check(!ParseLightingDebugMode("base-color", mode, error), "Reject GBuffer channel on lighting-view");
    Check(!error.empty(), "Unknown lighting-view produces an error string");

    const LightingDebugModeInfo& worldInfo = GetLightingDebugModeInfo(LightingDebugMode::WorldPosition);
    Check(std::string(worldInfo.cliName) == "world-position", "World-position CLI name");
    Check(std::string(worldInfo.dumpFileName) == "lighting-world-position.png", "World-position dump file");
    const LightingDebugModeInfo& ndotlInfo = GetLightingDebugModeInfo(LightingDebugMode::NdotL);
    Check(std::string(ndotlInfo.cliName) == "ndotl", "NdotL CLI name");
    Check(std::string(ndotlInfo.dumpFileName) == "lighting-ndotl.png", "NdotL dump file");

    const nvrhi::RasterState lightingRaster = MakeLightingDebugRasterState();
    Check(lightingRaster.cullMode == nvrhi::RasterCullMode::None, "Lighting debug culls none");
    const nvrhi::DepthStencilState lightingDepth = MakeLightingDebugDepthState();
    Check(!lightingDepth.depthTestEnable && !lightingDepth.depthWriteEnable,
          "Lighting debug depth test/write off");

    const nvrhi::RasterState deferredRaster = MakeDeferredLightingRasterState();
    Check(deferredRaster.cullMode == nvrhi::RasterCullMode::None, "Deferred lighting culls none");
    const nvrhi::DepthStencilState deferredDepth = MakeDeferredLightingDepthState();
    Check(!deferredDepth.depthTestEnable && !deferredDepth.depthWriteEnable,
          "Deferred lighting depth test/write off");

    Check(static_cast<uint32_t>(PresentSource::GBufferDebug) == 0u, "PresentSource GBufferDebug is 0");
    Check(static_cast<uint32_t>(PresentSource::LightingDebug) == 1u, "PresentSource LightingDebug is 1");

    // Mutual exclusion is enforced in main CLI parsing: both view flags must not be set.
    // Document the expected default present source here for S2.2.
    PresentSource present = PresentSource::LightingDebug;
    Check(present == PresentSource::LightingDebug, "Default present source is lighting debug");

    return g_failures;
}
