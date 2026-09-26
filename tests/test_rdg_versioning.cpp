#include "rdg/GraphBuilder.h"

#include <cstdio>
#include <string>
#include <type_traits>

using namespace renderlab::rdg;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* message)
    {
        if (condition)
        {
            std::printf("  PASS  %s\n", message);
            return;
        }
        std::printf("  FAIL  %s\n", message);
        ++g_failures;
    }

    template<typename Handle>
    Handle Create(GraphBuilder& graph, bool imported = false)
    {
        if constexpr (std::is_same_v<Handle, TextureHandle>)
        {
            const TextureDesc desc{"Resource", 8, 8, Format::RGBA8Unorm};
            return imported ? graph.ImportTexture(desc) : graph.CreateTexture(desc);
        }
        else
        {
            const BufferDesc desc{"Resource", 4, 16};
            return imported ? graph.ImportBuffer(desc) : graph.CreateBuffer(desc);
        }
    }

    template<typename Handle>
    void UseWrite(PassBuilder& pass, Handle handle)
    {
        pass.Use(handle, Access::RenderTarget);
    }

    template<typename Handle>
    void UseRead(PassBuilder& pass, Handle handle)
    {
        pass.Use(handle, Access::ShaderResource);
    }

    template<typename Handle>
    void Export(GraphBuilder& graph, Handle handle, Access access = Access::RenderTarget)
    {
        if constexpr (std::is_same_v<Handle, TextureHandle>)
        {
            graph.ExportTexture(handle, access);
        }
        else
        {
            graph.ExportBuffer(handle, access);
        }
    }

    void CheckError(const GraphBuilder& graph, ErrorCategory category, uint32_t passIndex)
    {
        const auto errors = graph.GetErrors();
        Check(!errors.empty() && errors.back().category == category, "Rejected call reports the expected category");
        if (!errors.empty())
        {
            Check(errors.back().resourceName == "Resource" && errors.back().passIndex == passIndex &&
                errors.back().passName == (passIndex == Error::kNoPass ? "" : graph.GetPass(passIndex).name),
                "Error identifies the resource and declaring pass");
        }
    }

    template<typename Handle>
    void RunIdentityCases()
    {
        {
            GraphBuilder graph;
            Handle initial = Create<Handle>(graph);
            Check(initial.index != kNullHandleIndex && initial.graphId == graph.GetGraphId(),
                "Create returns one identity for the resource");
            auto writer = graph.AddPass("Writer", PassFlags::Raster);
            UseWrite(writer, initial);
            Check(graph.GetErrors().empty() && graph.GetPass(0).accesses.size() == 1 &&
                graph.GetPass(0).accesses[0].index == initial.index &&
                graph.GetPass(0).accesses[0].access == Access::RenderTarget,
                "Write records the same handle");
            auto reader = graph.AddPass("Reader", PassFlags::Raster);
            UseRead(reader, initial);
            Export(graph, initial);
            Export(graph, initial);
            auto afterExport = graph.AddPass("AfterExport", PassFlags::Raster);
            UseRead(afterExport, initial);
            Check(graph.GetErrors().empty() && graph.GetResource(initial.index).exported,
                "The same handle supports later reads and a repeated export");
            auto again = graph.AddPass("WriteAgain", PassFlags::Raster);
            UseWrite(again, initial);
            Check(graph.GetErrors().empty() && graph.GetPass(again.PassIndex()).accesses.size() == 1,
                "A later pass may write the same resource after export");
            Export(graph, initial, Access::ShaderResource);
            CheckError(graph, ErrorCategory::IncompatibleAccess, Error::kNoPass);
            Check(graph.GetResource(initial.index).finalAccess == Access::RenderTarget,
                "A differing second export leaves the first final access");
        }
        {
            GraphBuilder graph;
            Handle input = Create<Handle>(graph, true);
            Handle intermediate = Create<Handle>(graph);
            Handle output = Create<Handle>(graph);
            auto first = graph.AddPass("First", PassFlags::Raster);
            UseRead(first, input);
            UseWrite(first, intermediate);
            auto second = graph.AddPass("Second", PassFlags::Raster);
            UseRead(second, intermediate);
            UseWrite(second, output);
            auto third = graph.AddPass("Third", PassFlags::Raster);
            UseRead(third, output);
            Export(graph, output);
            Check(graph.GetErrors().empty() && graph.GetResource(input.index).imported &&
                graph.GetPass(0).accesses.size() == 2 && graph.GetPass(1).accesses.size() == 2 &&
                graph.GetPass(2).accesses.size() == 1 && graph.GetResource(output.index).exported,
                "One identity chains an import through later reads and writes");
        }
        {
            GraphBuilder graph;
            Handle initial = Create<Handle>(graph);
            auto producer = graph.AddPass("Producer", PassFlags::Raster);
            UseWrite(producer, initial);
            auto firstReader = graph.AddPass("ReaderA", PassFlags::Raster);
            auto secondReader = graph.AddPass("ReaderB", PassFlags::Raster);
            UseRead(secondReader, initial);
            UseRead(firstReader, initial);
            UseRead(firstReader, initial);
            auto overwrite = graph.AddPass("Overwrite", PassFlags::Raster);
            UseWrite(overwrite, initial);
            auto readerC = graph.AddPass("ReaderC", PassFlags::Raster);
            UseRead(readerC, initial);
            Check(graph.GetErrors().empty() && graph.GetPass(1).accesses.size() == 2 &&
                graph.GetPass(overwrite.PassIndex()).accesses.size() == 1 &&
                graph.GetPass(readerC.PassIndex()).accesses.size() == 1,
                "Repeated reads stay recorded and a later write does not retire the handle");
        }
        {
            GraphBuilder graph;
            Handle initial = Create<Handle>(graph);
            auto reader = graph.AddPass("EarlyReader", PassFlags::Raster);
            UseRead(reader, initial);
            CheckError(graph, ErrorCategory::ReadBeforeProduce, reader.PassIndex());
            Export(graph, initial);
            CheckError(graph, ErrorCategory::ReadBeforeProduce, Error::kNoPass);
            Check(graph.GetPass(0).accesses.empty() && !graph.GetResource(initial.index).exported,
                "Read-before-write and unproduced export leave no access and no export");
            auto later = graph.AddPass("LaterProducer", PassFlags::Raster);
            UseWrite(later, initial);
            UseRead(reader, initial);
            CheckError(graph, ErrorCategory::ReadBeforeProduce, reader.PassIndex());
            Check(graph.GetPass(0).accesses.empty() && graph.GetPass(1).accesses.size() == 1,
                "A read in an earlier pass stays ReadBeforeProduce after a later write");
        }
        {
            GraphBuilder graph;
            Handle input = Create<Handle>(graph, true);
            auto external = graph.AddPass("ReadExternal", PassFlags::Raster);
            UseRead(external, input);
            auto writer = graph.AddPass("OverwriteExternal", PassFlags::Raster);
            UseWrite(writer, input);
            Check(graph.GetErrors().empty() && graph.GetPass(1).accesses.size() == 1,
                "Imported input supports a later write");
            UseWrite(writer, input);
            CheckError(graph, ErrorCategory::DuplicateWrite, writer.PassIndex());
            UseRead(writer, input);
            CheckError(graph, ErrorCategory::IncompatibleAccess, writer.PassIndex());
            Check(graph.GetPass(1).accesses.size() == 1,
                "Duplicate write and reading the same pass's write preserve the first write");
        }
        {
            GraphBuilder graph;
            Handle input = Create<Handle>(graph, true);
            auto pass = graph.AddPass("MixedAccess", PassFlags::Raster);
            UseRead(pass, input);
            UseWrite(pass, input);
            CheckError(graph, ErrorCategory::IncompatibleAccess, pass.PassIndex());
            Check(graph.GetPass(0).accesses.size() == 1 &&
                graph.GetPass(0).accesses[0].access == Access::ShaderResource,
                "Rejected mixed access preserves the prior read");
            Export(graph, input);
            Check(graph.GetResource(input.index).exported, "An imported resource can be exported without a write");
            auto invalid = graph.AddPass("", PassFlags::Raster);
            const size_t errorCount = graph.GetErrors().size();
            UseWrite(invalid, input);
            Check(graph.GetErrors().size() == errorCount, "Invalid PassBuilder use adds no error");
        }
    }
}

int RunRdgVersioningTests()
{
    std::printf("RenderLab RDG identity tests\n");
    RunIdentityCases<TextureHandle>();
    std::printf("RenderLab RDG buffer identity tests\n");
    RunIdentityCases<BufferHandle>();
    return g_failures;
}
