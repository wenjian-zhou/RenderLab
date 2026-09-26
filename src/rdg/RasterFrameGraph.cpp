#include "RasterFrameGraph.h"

namespace renderlab::rdg
{
    namespace
    {
        void UseGBuffer(PassBuilder& pass, RasterFrameGraph& graph, Access access)
        {
            pass.Use(graph.gbufferA, access);
            pass.Use(graph.gbufferB, access);
            pass.Use(graph.gbufferC, access);
            pass.Use(graph.gbufferDepth, access == Access::RenderTarget ? Access::DepthWrite : access);
        }
    }

    RasterFrameGraph BuildRasterFrameGraph(
        GraphBuilder& builder,
        uint32_t width,
        uint32_t height,
        RasterPresent present)
    {
        RasterFrameGraph graph;
        graph.gbufferA = builder.CreateTexture({"GBufferA", width, height, Format::SRGBA8Unorm});
        graph.gbufferB = builder.CreateTexture({"GBufferB", width, height, Format::RGBA16Float});
        graph.gbufferC = builder.CreateTexture({"GBufferC", width, height, Format::RGBA8Unorm});
        graph.gbufferDepth = builder.CreateTexture({"GBufferDepth", width, height, Format::D32Float});

        graph.outputImported =
            builder.ImportTexture({"BackBuffer", width, height, Format::SRGBA8Unorm});
        builder.SetInitialAccess(graph.outputImported, Access::Present);

        PassBuilder gbufferPass = builder.AddPass("GBuffer", PassFlags::Raster);
        graph.gbufferPassIndex = gbufferPass.PassIndex();
        UseGBuffer(gbufferPass, graph, Access::RenderTarget);

        const bool withHdr = present != RasterPresent::GBufferDebug;
        graph.hasHdr = withHdr;
        if (withHdr)
        {
            graph.hdrCreated =
                builder.CreateTexture({"HDRSceneColor", width, height, Format::RGBA16Float});
            graph.hdrWritten = graph.hdrCreated;
            PassBuilder lightingPass = builder.AddPass("DeferredLighting", PassFlags::Raster);
            graph.deferredLightingPassIndex = lightingPass.PassIndex();
            UseGBuffer(lightingPass, graph, Access::ShaderResource);
            lightingPass.Use(graph.hdrCreated, Access::RenderTarget);
        }

        const char* presentName = "PostProcess";
        if (present == RasterPresent::LightingDebug)
        {
            presentName = "LightingDebug";
        }
        else if (present == RasterPresent::GBufferDebug)
        {
            presentName = "GBufferDebug";
        }

        PassBuilder presentPass = builder.AddPass(presentName, PassFlags::Raster);
        graph.presentPassIndex = presentPass.PassIndex();
        if (present != RasterPresent::Final)
        {
            UseGBuffer(presentPass, graph, Access::ShaderResource);
        }
        if (withHdr)
        {
            presentPass.Use(graph.hdrWritten, Access::ShaderResource);
        }
        presentPass.Use(graph.outputImported, Access::RenderTarget);
        graph.outputWritten = graph.outputImported;
        builder.ExportTexture(graph.outputWritten, Access::Present);
        return graph;
    }
}
