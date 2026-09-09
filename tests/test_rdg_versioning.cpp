#include "rdg/GraphBuilder.h"

#include <cstdio>
#include <string>
#include <type_traits>
#include <vector>

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
            const TextureDesc desc{"Resource", 8, 8, Format::Rgba8Unorm};
            return imported ? graph.ImportTexture(desc) : graph.CreateTexture(desc);
        }
        else
        {
            const BufferDesc desc{"Resource", 4, 16};
            return imported ? graph.ImportBuffer(desc) : graph.CreateBuffer(desc);
        }
    }

    template<typename Handle>
    void Export(GraphBuilder& graph, Handle handle)
    {
        if constexpr (std::is_same_v<Handle, TextureHandle>)
        {
            graph.ExportTexture(handle);
        }
        else
        {
            graph.ExportBuffer(handle);
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
    void RunVersionCases()
    {
        {
            GraphBuilder graph;
            Handle initial = Create<Handle>(graph);
            const ResourceRecord& resource = graph.GetResource(initial.index);
            Check(resource.versions.size() == 1 && !resource.versions[0].produced &&
                resource.versions[0].producerPass == Error::kNoPass && resource.versions[0].readerPasses.empty(),
                "Internal v0 is an unproduced allocation token");
            auto writer = graph.AddPass("Writer", PassFlags::Raster);
            Handle output = writer.Write(initial);
            Check(output.index == initial.index && output.graphId == initial.graphId && output.version == 1,
                "Single writer returns a distinct version on the same resource and graph");
            Check(resource.currentVersion == 1 && resource.versions.size() == 2 &&
                resource.versions[1].produced && resource.versions[1].producerPass == 0 &&
                !resource.versions[0].produced, "Write records one producer and preserves v0");
            Check(graph.GetPass(0).accesses.size() == 1 && graph.GetPass(0).accesses[0].version == output.version &&
                graph.GetPass(0).accesses[0].mode == AccessMode::Write, "Write access names the output version");
            auto reader = graph.AddPass("Reader", PassFlags::Raster);
            reader.Read(output);
            Export(graph, output);
            Export(graph, output);
            graph.AddPass("ReadExport", PassFlags::Raster).Read(output);
            Check(graph.GetErrors().empty() && resource.exported, "Produced output supports readers and idempotent export");
            Check(resource.versions[1].readerPasses == std::vector<uint32_t>{1, 2},
                "Export permits subsequent reads without adding a synthetic reader");
            const std::string snapshot = graph.DumpVersions();
            Handle rejected = graph.AddPass("OverwriteExport", PassFlags::Raster).Write(output);
            CheckError(graph, ErrorCategory::IncompatibleAccess, 3);
            Check(rejected.IsNull() && graph.DumpVersions() == snapshot && graph.GetPass(3).accesses.empty(),
                "Export pins the final version; rejected overwrite changes no metadata");
        }
        {
            GraphBuilder graph;
            Handle input = Create<Handle>(graph, true);
            Handle intermediate = Create<Handle>(graph);
            Handle output = Create<Handle>(graph);
            auto first = graph.AddPass("First", PassFlags::Raster);
            first.Read(input);
            intermediate = first.Write(intermediate);
            auto second = graph.AddPass("Second", PassFlags::Raster);
            second.Read(intermediate);
            output = second.Write(output);
            graph.AddPass("Third", PassFlags::Raster).Read(output);
            Export(graph, output);
            const auto& inputVersion = graph.GetResource(input.index).versions[0];
            Check(graph.GetErrors().empty() && inputVersion.produced && inputVersion.producerPass == Error::kNoPass,
                "Read chain starts at an externally produced imported v0");
            Check(inputVersion.readerPasses == std::vector<uint32_t>{0} &&
                graph.GetResource(intermediate.index).versions[1].readerPasses == std::vector<uint32_t>{1} &&
                graph.GetResource(output.index).versions[1].readerPasses == std::vector<uint32_t>{2},
                "Read chain associates each consumed version with its reader");
            const std::string dump = graph.DumpVersions();
            Check(dump.find("v0 producer=external readers=[0 'First'] current") != std::string::npos &&
                dump.find("v1 producer=0 'First' readers=[1 'Second'] current") != std::string::npos &&
                dump.find("v1 producer=1 'Second' readers=[2 'Third'] current exported") != std::string::npos &&
                dump.find("v0 producer=unproduced readers=[]") != std::string::npos,
                "Text dump explains every consumed version, external inputs, and exported output");
            Check(dump == graph.DumpVersions(), "Version dump is deterministic");
        }
        {
            GraphBuilder graph;
            Handle initial = Create<Handle>(graph);
            Handle firstVersion = graph.AddPass("Producer", PassFlags::Raster).Write(initial);
            auto firstReader = graph.AddPass("ReaderA", PassFlags::Raster);
            auto secondReader = graph.AddPass("ReaderB", PassFlags::Raster);
            secondReader.Read(firstVersion);
            firstReader.Read(firstVersion);
            firstReader.Read(firstVersion);
            Handle secondVersion = graph.AddPass("Overwrite", PassFlags::Raster).Write(firstVersion);
            graph.AddPass("ReaderC", PassFlags::Raster).Read(secondVersion);
            const ResourceRecord& resource = graph.GetResource(initial.index);
            Check(graph.GetErrors().empty() && secondVersion.version == 2 && resource.versions.size() == 3,
                "WAW succeeds across passes without an implicit read");
            Check(resource.versions[1].producerPass == 0 &&
                resource.versions[1].readerPasses == std::vector<uint32_t>{2, 1} &&
                resource.versions[2].producerPass == 3 &&
                resource.versions[2].readerPasses == std::vector<uint32_t>{4},
                "WAW preserves old producer and multiple readers for future WAR/WAW edges");
            Check(graph.GetPass(1).accesses.size() == 2 && graph.GetPass(1).accesses[0].version == 1,
                "Repeated reads retain declarations but deduplicate the version reader list");
            const std::string snapshot = graph.DumpVersions();
            auto stale = graph.AddPass("Stale", PassFlags::Raster);
            stale.Read(firstVersion);
            CheckError(graph, ErrorCategory::SupersededUse, stale.PassIndex());
            Check(stale.Write(firstVersion).IsNull(), "Writing an old version cannot branch resource history");
            CheckError(graph, ErrorCategory::SupersededUse, stale.PassIndex());
            Export(graph, firstVersion);
            CheckError(graph, ErrorCategory::SupersededUse, Error::kNoPass);
            stale.Read(initial);
            CheckError(graph, ErrorCategory::SupersededUse, stale.PassIndex());
            Handle forged = secondVersion;
            forged.version += 10;
            stale.Read(forged);
            CheckError(graph, ErrorCategory::StaleVersion, stale.PassIndex());
            Check(graph.GetErrors().size() == 5 && graph.DumpVersions() == snapshot && graph.GetPass(5).accesses.empty(),
                "Rejected superseded and forged uses leave accesses and history intact");
        }
        {
            GraphBuilder graph;
            Handle initial = Create<Handle>(graph);
            auto reader = graph.AddPass("EarlyReader", PassFlags::Raster);
            reader.Read(initial);
            CheckError(graph, ErrorCategory::ReadBeforeProduce, reader.PassIndex());
            Export(graph, initial);
            CheckError(graph, ErrorCategory::ReadBeforeProduce, Error::kNoPass);
            Check(graph.GetPass(0).accesses.empty() && !graph.GetResource(initial.index).exported &&
                graph.GetResource(initial.index).versions[0].readerPasses.empty(),
                "Read-before-write and unproduced export leave no accesses, readers, or root flag");
            Handle output = graph.AddPass("LaterProducer", PassFlags::Raster).Write(initial);
            reader.Read(output);
            Check(graph.GetErrors().size() == 2 && graph.GetPass(0).accesses.size() == 1 &&
                graph.GetResource(initial.index).versions[1].readerPasses == std::vector<uint32_t>{0},
                "Pass indices are identities; an older builder may read an already produced version");
        }
        {
            GraphBuilder graph;
            Handle input = Create<Handle>(graph, true);
            graph.AddPass("ReadExternal", PassFlags::Raster).Read(input);
            auto writer = graph.AddPass("OverwriteExternal", PassFlags::Raster);
            Handle output = writer.Write(input);
            Check(output.version == 1 && graph.GetResource(input.index).versions[0].produced &&
                graph.GetResource(input.index).versions[0].readerPasses == std::vector<uint32_t>{0},
                "Imported input supports overwrite while preserving external provenance");
            Check(writer.Write(output).IsNull(), "Repeated same-pass writes return null even with the newest handle");
            CheckError(graph, ErrorCategory::DuplicateWrite, writer.PassIndex());
            writer.Read(output);
            CheckError(graph, ErrorCategory::IncompatibleAccess, writer.PassIndex());
            Check(graph.GetResource(input.index).versions.size() == 2 && graph.GetPass(1).accesses.size() == 1,
                "Duplicate write and reading own output preserve the successful write");
        }
        {
            GraphBuilder graph;
            Handle input = Create<Handle>(graph, true);
            auto pass = graph.AddPass("MixedAccess", PassFlags::Raster);
            pass.Read(input);
            Check(pass.Write(input).IsNull(), "Same-pass read then write is incompatible");
            CheckError(graph, ErrorCategory::IncompatibleAccess, pass.PassIndex());
            Check(graph.GetResource(input.index).currentVersion == 0 && graph.GetPass(0).accesses.size() == 1,
                "Rejected mixed access preserves the prior read");
            Export(graph, input);
            Check(graph.GetResource(input.index).exported, "Unmodified imported v0 can be exported");
            auto invalid = graph.AddPass("", PassFlags::Raster);
            const size_t errorCount = graph.GetErrors().size();
            Check(invalid.Write(input).IsNull() && graph.GetErrors().size() == errorCount,
                "Invalid PassBuilder writes return null without adding errors");
        }
    }
}

int RunRdgVersioningTests()
{
    std::printf("RenderLab S4.2 texture versioning tests\n");
    RunVersionCases<TextureHandle>();
    std::printf("RenderLab S4.2 buffer versioning tests\n");
    RunVersionCases<BufferHandle>();
    return g_failures;
}
