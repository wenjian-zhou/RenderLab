#include "ToneMapGraph.h"

namespace renderlab::rdg
{
    ToneMapGraph BuildToneMapGraph(
        GraphBuilder& builder,
        uint32_t width,
        uint32_t height,
        ToneMapOutput output)
    {
        ToneMapGraph graph;
        graph.hdrSceneColor =
            builder.ImportTexture({"HDRSceneColor", width, height, Format::RGBA16Float});
        builder.SetInitialAccess(graph.hdrSceneColor, Access::RenderTarget);

        const bool exportOutput = output == ToneMapOutput::BackBuffer;
        const char* outputName = exportOutput ? "BackBuffer" : "PostProcessColor";
        graph.outputImported =
            builder.ImportTexture({outputName, width, height, Format::SRGBA8Unorm});
        graph.outputWritten = graph.outputImported;
        builder.SetInitialAccess(
            graph.outputImported, exportOutput ? Access::Present : Access::RenderTarget);

        PassBuilder postPass = builder.AddPass("PostProcess", PassFlags::Raster);
        graph.postProcessPassIndex = postPass.PassIndex();
        postPass.Use(graph.hdrSceneColor, Access::ShaderResource);
        postPass.Use(graph.outputImported, Access::RenderTarget);
        if (exportOutput)
        {
            builder.ExportTexture(graph.outputWritten, Access::Present);
        }
        return graph;
    }
}
