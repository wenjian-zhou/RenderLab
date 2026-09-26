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
        builder.SetInitialAccess(backBuffer, Access::Present);

        PassBuilder gbufferPass = builder.AddPass("GBuffer", PassFlags::Raster);
        gbufferPass.Use(gbufferA, Access::RenderTarget);
        gbufferPass.Use(gbufferB, Access::RenderTarget);
        gbufferPass.Use(gbufferC, Access::RenderTarget);
        gbufferPass.Use(gbufferDepth, Access::DepthWrite);

        PassBuilder deferredPass = builder.AddPass("DeferredLighting", PassFlags::Raster);
        deferredPass.Use(gbufferA, Access::ShaderResource);
        deferredPass.Use(gbufferB, Access::ShaderResource);
        deferredPass.Use(gbufferC, Access::ShaderResource);
        deferredPass.Use(gbufferDepth, Access::ShaderResource);
        deferredPass.Use(hdrSceneColor, Access::RenderTarget);

        PassBuilder postPass = builder.AddPass("PostProcess", PassFlags::Raster);
        postPass.Use(hdrSceneColor, Access::ShaderResource);
        postPass.Use(backBuffer, Access::RenderTarget);

        builder.ExportTexture(backBuffer, Access::Present);
    }
}
