#include "M1ShapedGraph.h"

namespace renderlab::rdg
{
    void BuildM1ShapedGraph(GraphBuilder& builder)
    {
        TextureHandle backBuffer = builder.ImportTexture({"BackBuffer", 1280, 720, Format::SRGBA8Unorm});
        TextureHandle gbufferA = builder.CreateTexture({"GBufferA", 1280, 720, Format::SRGBA8Unorm});
        TextureHandle gbufferB = builder.CreateTexture({"GBufferB", 1280, 720, Format::RGBA16Float});
        TextureHandle gbufferC = builder.CreateTexture({"GBufferC", 1280, 720, Format::RGBA8Unorm});
        TextureHandle gbufferDepth = builder.CreateTexture({"GBufferDepth", 1280, 720, Format::D32Float});
        TextureHandle hdrSceneColor = builder.CreateTexture({"HDRSceneColor", 1280, 720, Format::RGBA16Float});

        PassBuilder gbufferPass = builder.AddPass("GBuffer", PassFlags::Raster);
        gbufferA = gbufferPass.Write(gbufferA);
        gbufferB = gbufferPass.Write(gbufferB);
        gbufferC = gbufferPass.Write(gbufferC);
        gbufferDepth = gbufferPass.Write(gbufferDepth);

        PassBuilder deferredPass = builder.AddPass("DeferredLighting", PassFlags::Raster);
        deferredPass.Read(gbufferA);
        deferredPass.Read(gbufferB);
        deferredPass.Read(gbufferC);
        deferredPass.Read(gbufferDepth);
        hdrSceneColor = deferredPass.Write(hdrSceneColor);

        PassBuilder postPass = builder.AddPass("PostProcess", PassFlags::Raster);
        postPass.Read(hdrSceneColor);
        backBuffer = postPass.Write(backBuffer);

        builder.ExportTexture(backBuffer);
    }
}
