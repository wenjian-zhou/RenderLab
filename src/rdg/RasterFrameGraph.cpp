#include "RasterFrameGraph.h"

namespace renderlab::rdg
{
    namespace
    {
        void ReadGBuffer(PassBuilder& pass, RasterFrameGraph& graph)
        {
            pass.Read(graph.gbufferA);
            pass.Read(graph.gbufferB);
            pass.Read(graph.gbufferC);
            pass.Read(graph.gbufferDepth);
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

        PassBuilder gbufferPass = builder.AddPass("GBuffer", PassFlags::Raster);
        graph.gbufferPassIndex = gbufferPass.PassIndex();
        graph.gbufferA = gbufferPass.Write(graph.gbufferA);
        graph.gbufferB = gbufferPass.Write(graph.gbufferB);
        graph.gbufferC = gbufferPass.Write(graph.gbufferC);
        graph.gbufferDepth = gbufferPass.Write(graph.gbufferDepth);

        const bool withHdr = present != RasterPresent::GBufferDebug;
        graph.hasHdr = withHdr;
        if (withHdr)
        {
            graph.hdrCreated =
                builder.CreateTexture({"HDRSceneColor", width, height, Format::RGBA16Float});
            PassBuilder lightingPass = builder.AddPass("DeferredLighting", PassFlags::Raster);
            graph.deferredLightingPassIndex = lightingPass.PassIndex();
            ReadGBuffer(lightingPass, graph);
            graph.hdrWritten = lightingPass.Write(graph.hdrCreated);
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
            ReadGBuffer(presentPass, graph);
        }
        if (withHdr)
        {
            presentPass.Read(graph.hdrWritten);
        }
        graph.outputWritten = presentPass.Write(graph.outputImported);
        builder.ExportTexture(graph.outputWritten);
        return graph;
    }
}
