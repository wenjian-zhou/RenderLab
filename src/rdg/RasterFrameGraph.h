#pragma once

#include "GraphBuilder.h"

#include <cstdint>

namespace renderlab::rdg
{
    enum class RasterPresent : uint8_t
    {
        Final,
        LightingDebug,
        GBufferDebug,
    };

    struct RasterFrameGraph
    {
        TextureHandle gbufferA;
        TextureHandle gbufferB;
        TextureHandle gbufferC;
        TextureHandle gbufferDepth;
        TextureHandle hdrCreated;
        TextureHandle hdrWritten;
        TextureHandle outputImported;
        TextureHandle outputWritten;
        uint32_t gbufferPassIndex = 0;
        uint32_t deferredLightingPassIndex = Error::kNoPass;
        uint32_t presentPassIndex = 0;
        bool hasHdr = false;
    };

    // Full raster frame declarations. GBuffer and depth are Create*. HDR is
    // Create* only when the present pass reads it (Final, LightingDebug).
    // GBufferDebug omits HDR and DeferredLighting so compile does not see a
    // zero-use allocation. BackBuffer is imported and exported.
    RasterFrameGraph BuildRasterFrameGraph(
        GraphBuilder& builder,
        uint32_t width,
        uint32_t height,
        RasterPresent present);
}
