#pragma once

#include "GraphBuilder.h"
#include "ToneMapGraph.h"

#include <cstdint>

namespace renderlab::rdg
{
    struct LightingPresentGraph
    {
        TextureHandle gbufferA;
        TextureHandle gbufferB;
        TextureHandle gbufferC;
        TextureHandle gbufferDepth;
        TextureHandle hdrCreated;
        TextureHandle hdrWritten;
        TextureHandle outputImported;
        TextureHandle outputWritten;
        uint32_t deferredLightingPassIndex = 0;
        uint32_t lightingDebugPassIndex = 0;
        uint32_t postProcessPassIndex = 0;
    };

    // Lighting-to-present chain: imported GBuffer/depth, Create* HDR,
    // DeferredLighting then PostProcess, plus a declare-only LightingDebug
    // branch that culls. Output is BackBuffer exported Present, or
    // PostProcessColor dump target.
    LightingPresentGraph BuildLightingPresentGraph(
        GraphBuilder& builder,
        uint32_t width,
        uint32_t height,
        ToneMapOutput output);
}
