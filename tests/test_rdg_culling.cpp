#include "rdg/GraphCompiler.h"

#include <cstdio>
#include <initializer_list>
#include <span>
#include <string>

using namespace renderlab::rdg;

namespace
{
    int failures = 0;

    void Check(bool condition, const char* message)
    {
        std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition)
        {
            ++failures;
        }
    }

    TextureHandle MakeTexture(GraphBuilder& graph, const char* name = "Resource")
    {
        return graph.CreateTexture(TextureDesc{name, 8, 8, Format::RGBA8Unorm});
    }

    const PassCullState* FindCull(std::span<const PassCullState> states, uint32_t passIndex)
    {
        for (const PassCullState& state : states)
        {
            if (state.passIndex == passIndex)
            {
                return &state;
            }
        }
        return nullptr;
    }

    bool HasCull(const CompileResult& result, uint32_t passIndex, bool culled, CullReason reason)
    {
        const PassCullState* state = FindCull(result.GetPassCullStates(), passIndex);
        return state != nullptr && state->culled == culled && state->reason == reason;
    }

    bool LiveOrderIs(const CompileResult& result, std::initializer_list<uint32_t> expected)
    {
        const auto live = result.GetLivePassOrder();
        if (live.size() != expected.size())
        {
            return false;
        }
        size_t index = 0;
        for (const uint32_t passIndex : expected)
        {
            if (live[index++] != passIndex)
            {
                return false;
            }
        }
        return true;
    }
}

int RunRdgCullingTests()
{
    std::printf("RenderLab S4.4 culling tests\n");
    {
        GraphBuilder graph;
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && result.GetPassCullStates().empty() && result.GetLivePassOrder().empty() &&
            result.Dump().find("cull:") != std::string::npos,
            "Empty graph has no cull states");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Unused", PassFlags::Raster).Use(resource, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && result.GetPassOrder().size() == 1 && result.GetLivePassOrder().empty() &&
            HasCull(result, 0, true, CullReason::UnusedLeaf) &&
            result.GetResourceLifetimes().empty() && result.GetErrors().size() == 1 &&
            result.GetErrors()[0].category == ErrorCategory::ZeroUseAllocation,
            "Unused leaf is culled");
    }
    {
        GraphBuilder graph;
        TextureHandle intermediate = MakeTexture(graph, "Intermediate");
        TextureHandle unusedOut = MakeTexture(graph, "UnusedOut");
        auto producer = graph.AddPass("Producer", PassFlags::Raster);
        auto consumer = graph.AddPass("Consumer", PassFlags::Raster);
        producer.Use(intermediate, Access::RenderTarget);
        consumer.Use(intermediate, Access::ShaderResource);
        consumer.Use(unusedOut, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && result.GetPassOrder().size() == 2 && result.GetLivePassOrder().empty() &&
            HasCull(result, 0, true, CullReason::UnusedChain) &&
            HasCull(result, 1, true, CullReason::UnusedLeaf) &&
            result.GetResourceLifetimes().empty() && result.GetErrors().size() == 2 &&
            result.GetErrors()[0].category == ErrorCategory::ZeroUseAllocation &&
            result.GetErrors()[0].passIndex == Error::kNoPass &&
            result.GetErrors()[0].resourceName == "Intermediate" &&
            result.GetErrors()[1].category == ErrorCategory::ZeroUseAllocation &&
            result.GetErrors()[1].passIndex == Error::kNoPass &&
            result.GetErrors()[1].resourceName == "UnusedOut",
            "Unused chain culls producer as unused-chain and consumer as unused-leaf");
    }
    {
        GraphBuilder graph;
        TextureHandle intermediate = MakeTexture(graph, "Intermediate");
        TextureHandle output = MakeTexture(graph, "Output");
        auto producer = graph.AddPass("Producer", PassFlags::Raster);
        auto consumer = graph.AddPass("Consumer", PassFlags::Raster);
        producer.Use(intermediate, Access::RenderTarget);
        consumer.Use(intermediate, Access::ShaderResource);
        consumer.Use(output, Access::RenderTarget);
        graph.ExportTexture(output, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && LiveOrderIs(result, {0, 1}) &&
            HasCull(result, 1, false, CullReason::RootOutput) &&
            HasCull(result, 0, false, CullReason::Producer),
            "Exported write keeps its RAW producer chain live");
    }
    {
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::RGBA8Unorm});
        graph.AddPass("Writer", PassFlags::Raster).Use(imported, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && LiveOrderIs(result, {0}) &&
            HasCull(result, 0, false, CullReason::RootOutput),
            "Imported write is a root-output without export");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        auto producer = graph.AddPass("Producer", PassFlags::Raster);
        auto forced = graph.AddPass("Forced", PassFlags::NeverCull);
        auto unused = graph.AddPass("Unused", PassFlags::Raster);
        producer.Use(resource, Access::RenderTarget);
        forced.Use(resource, Access::ShaderResource);
        unused.Use(resource, Access::ShaderResource);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && LiveOrderIs(result, {0, 1}) &&
            HasCull(result, 1, false, CullReason::RootNeverCull) &&
            HasCull(result, 0, false, CullReason::Producer) &&
            HasCull(result, 2, true, CullReason::UnusedLeaf),
            "NeverCull keeps itself and producers; unused sibling consumers still cull");
    }
    {
        GraphBuilder graph;
        TextureHandle shared = MakeTexture(graph, "Shared");
        TextureHandle output = MakeTexture(graph, "Output");
        auto producer = graph.AddPass("Producer", PassFlags::Raster);
        auto live = graph.AddPass("Live", PassFlags::Raster);
        auto unused = graph.AddPass("Unused", PassFlags::Raster);
        producer.Use(shared, Access::RenderTarget);
        live.Use(shared, Access::ShaderResource);
        live.Use(output, Access::RenderTarget);
        unused.Use(shared, Access::ShaderResource);
        graph.ExportTexture(output, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && LiveOrderIs(result, {0, 1}) &&
            HasCull(result, 1, false, CullReason::RootOutput) &&
            HasCull(result, 0, false, CullReason::Producer) &&
            HasCull(result, 2, true, CullReason::UnusedLeaf),
            "Shared producer stays live when one consumer is a root and the other is unused");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Unused", PassFlags::Raster).Use(resource, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph, CompileOptions{.disableCulling = true});
        Check(result.IsSuccess() && LiveOrderIs(result, {0}) &&
            HasCull(result, 0, false, CullReason::CullingDisabled) &&
            result.Dump().find("culling-disabled") != std::string::npos,
            "Disabled culling keeps unused passes live");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        auto writer = graph.AddPass("Writer", PassFlags::Raster);
        writer.Use(resource, Access::RenderTarget);
        auto reader = graph.AddPass("Reader", PassFlags::Raster);
        reader.Use(resource, Access::ShaderResource);
        auto overwrite = graph.AddPass("Overwrite", PassFlags::Raster);
        overwrite.Use(resource, Access::RenderTarget);
        TextureHandle output = MakeTexture(graph, "Output");
        auto consumer = graph.AddPass("Consumer", PassFlags::Raster);
        consumer.Use(resource, Access::ShaderResource);
        consumer.Use(output, Access::RenderTarget);
        graph.ExportTexture(output, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && LiveOrderIs(result, {2, 3}) &&
            HasCull(result, 3, false, CullReason::RootOutput) &&
            HasCull(result, 2, false, CullReason::Producer) &&
            HasCull(result, 0, true, CullReason::UnusedChain) &&
            HasCull(result, 1, true, CullReason::UnusedLeaf),
            "An overwritten writer is culled when nothing live reads it before the next write");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Unused", PassFlags::Raster).Use(resource, Access::RenderTarget);
        const CompileResult first = GraphCompiler::Compile(graph);
        Check(first.Dump() == GraphCompiler::Compile(graph).Dump() &&
            first.Dump().find("cull:") != std::string::npos &&
            first.Dump().find("unused-leaf") != std::string::npos &&
            first.Dump().find("lifetime: skipped") != std::string::npos,
            "Cull dump is deterministic");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Invalid", PassFlags::Raster).Use(resource, Access::ShaderResource);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && result.GetPassCullStates().empty() && result.GetLivePassOrder().empty() &&
            result.Dump().find("cull: skipped") != std::string::npos &&
            result.Dump().find("lifetime: skipped") != std::string::npos,
            "Builder errors skip cull results");
    }
    {
        GraphBuilder graph;
        TextureHandle first = MakeTexture(graph, "First");
        TextureHandle second = MakeTexture(graph, "Second");
        auto passA = graph.AddPass("A", PassFlags::Raster);
        auto passB = graph.AddPass("B", PassFlags::Raster);
        passA.Use(first, Access::RenderTarget);
        passB.Use(second, Access::RenderTarget);
        passA.Use(second, Access::ShaderResource);
        passB.Use(first, Access::ShaderResource);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && result.GetErrors()[0].category == ErrorCategory::ReadBeforeProduce &&
            result.GetPassCullStates().empty() && result.GetLivePassOrder().empty() &&
            result.Dump().find("cull: skipped") != std::string::npos &&
            result.Dump().find("lifetime: skipped") != std::string::npos &&
            result.Dump().find("cycle:") == std::string::npos,
            "A backward read is ReadBeforeProduce and skips cull results");
    }
    return failures;
}
