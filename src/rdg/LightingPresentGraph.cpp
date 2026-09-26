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
        builder.SetInitialAccess(graph.gbufferA, Access::RenderTarget);
        builder.SetInitialAccess(graph.gbufferB, Access::RenderTarget);
        builder.SetInitialAccess(graph.gbufferC, Access::RenderTarget);
        builder.SetInitialAccess(graph.gbufferDepth, Access::DepthWrite);
        graph.hdrCreated =
            builder.CreateTexture({"HDRSceneColor", width, height, Format::RGBA16Float});
        graph.hdrWritten = graph.hdrCreated;

        const bool exportOutput = output == ToneMapOutput::BackBuffer;
        const char* outputName = exportOutput ? "BackBuffer" : "PostProcessColor";
        graph.outputImported =
            builder.ImportTexture({outputName, width, height, Format::SRGBA8Unorm});
        graph.outputWritten = graph.outputImported;
        builder.SetInitialAccess(
            graph.outputImported, exportOutput ? Access::Present : Access::RenderTarget);

        PassBuilder lightingPass = builder.AddPass("DeferredLighting", PassFlags::Raster);
        graph.deferredLightingPassIndex = lightingPass.PassIndex();
        lightingPass.Use(graph.gbufferA, Access::ShaderResource);
        lightingPass.Use(graph.gbufferB, Access::ShaderResource);
        lightingPass.Use(graph.gbufferC, Access::ShaderResource);
        lightingPass.Use(graph.gbufferDepth, Access::ShaderResource);
        lightingPass.Use(graph.hdrCreated, Access::RenderTarget);

        PassBuilder debugPass = builder.AddPass("LightingDebug", PassFlags::Raster);
        graph.lightingDebugPassIndex = debugPass.PassIndex();
        debugPass.Use(graph.gbufferA, Access::ShaderResource);
        debugPass.Use(graph.gbufferB, Access::ShaderResource);
        debugPass.Use(graph.gbufferC, Access::ShaderResource);
        debugPass.Use(graph.gbufferDepth, Access::ShaderResource);
        debugPass.Use(graph.hdrWritten, Access::ShaderResource);

        PassBuilder postPass = builder.AddPass("PostProcess", PassFlags::Raster);
        graph.postProcessPassIndex = postPass.PassIndex();
        postPass.Use(graph.hdrWritten, Access::ShaderResource);
        postPass.Use(graph.outputImported, Access::RenderTarget);
        if (exportOutput)
        {
            builder.ExportTexture(graph.outputWritten, Access::Present);
        }
        return graph;
    }
}
