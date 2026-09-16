#pragma once

#include "GraphBuilder.h"

#include <cstdint>

namespace renderlab::rdg
{
    enum class ToneMapOutput : uint8_t
    {
        BackBuffer,
        DumpTarget,
    };

    struct ToneMapGraph
    {
        TextureHandle hdrSceneColor;
        TextureHandle outputImported;
        TextureHandle outputWritten;
        uint32_t postProcessPassIndex = 0;
    };

    // One-pass PostProcess leaf: imported HDRSceneColor plus an imported
    // output (BackBuffer exported Present, or PostProcessColor dump target).
    ToneMapGraph BuildToneMapGraph(
        GraphBuilder& builder,
        uint32_t width,
        uint32_t height,
        ToneMapOutput output);
}
