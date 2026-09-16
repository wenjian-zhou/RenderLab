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

        const bool exportOutput = output == ToneMapOutput::BackBuffer;
        const char* outputName = exportOutput ? "BackBuffer" : "PostProcessColor";
        graph.outputImported =
            builder.ImportTexture({outputName, width, height, Format::SRGBA8Unorm});

        PassBuilder postPass = builder.AddPass("PostProcess", PassFlags::Raster);
        graph.postProcessPassIndex = postPass.PassIndex();
        postPass.Read(graph.hdrSceneColor);
        graph.outputWritten = postPass.Write(graph.outputImported);
        if (exportOutput)
        {
            builder.ExportTexture(graph.outputWritten);
        }
        return graph;
    }
}
