#include "LightingPresentGraph.h"

namespace renderlab::rdg
{
    LightingPresentGraph BuildLightingPresentGraph(
        GraphBuilder& builder,
        uint32_t width,
        uint32_t height,
        ToneMapOutput output)
    {
        LightingPresentGraph graph;
        graph.gbufferA = builder.ImportTexture({"GBufferA", width, height, Format::SRGBA8Unorm});
        graph.gbufferB = builder.ImportTexture({"GBufferB", width, height, Format::RGBA16Float});
        graph.gbufferC = builder.ImportTexture({"GBufferC", width, height, Format::RGBA8Unorm});
        graph.gbufferDepth = builder.ImportTexture({"GBufferDepth", width, height, Format::D32Float});
        graph.hdrCreated =
            builder.CreateTexture({"HDRSceneColor", width, height, Format::RGBA16Float});

        const bool exportOutput = output == ToneMapOutput::BackBuffer;
        const char* outputName = exportOutput ? "BackBuffer" : "PostProcessColor";
        graph.outputImported =
            builder.ImportTexture({outputName, width, height, Format::SRGBA8Unorm});

        PassBuilder lightingPass = builder.AddPass("DeferredLighting", PassFlags::Raster);
        graph.deferredLightingPassIndex = lightingPass.PassIndex();
        lightingPass.Read(graph.gbufferA);
        lightingPass.Read(graph.gbufferB);
        lightingPass.Read(graph.gbufferC);
        lightingPass.Read(graph.gbufferDepth);
        graph.hdrWritten = lightingPass.Write(graph.hdrCreated);

        PassBuilder debugPass = builder.AddPass("LightingDebug", PassFlags::Raster);
        graph.lightingDebugPassIndex = debugPass.PassIndex();
        debugPass.Read(graph.gbufferA);
        debugPass.Read(graph.gbufferB);
        debugPass.Read(graph.gbufferC);
        debugPass.Read(graph.gbufferDepth);
        debugPass.Read(graph.hdrWritten);

        PassBuilder postPass = builder.AddPass("PostProcess", PassFlags::Raster);
        graph.postProcessPassIndex = postPass.PassIndex();
        postPass.Read(graph.hdrWritten);
        graph.outputWritten = postPass.Write(graph.outputImported);
        if (exportOutput)
        {
            builder.ExportTexture(graph.outputWritten);
        }
        return graph;
    }
}
