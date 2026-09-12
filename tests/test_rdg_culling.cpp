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
        return graph.CreateTexture(TextureDesc{name, 8, 8, Format::Rgba8Unorm});
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
        graph.AddPass("Unused", PassFlags::Raster).Write(resource);
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
        intermediate = producer.Write(intermediate);
        consumer.Read(intermediate);
        unusedOut = consumer.Write(unusedOut);
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
        intermediate = producer.Write(intermediate);
        consumer.Read(intermediate);
        output = consumer.Write(output);
        graph.ExportTexture(output);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && LiveOrderIs(result, {0, 1}) &&
            HasCull(result, 1, false, CullReason::RootOutput) &&
            HasCull(result, 0, false, CullReason::Producer),
            "Exported write keeps its RAW producer chain live");
    }
    {
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::Rgba8Unorm});
        graph.AddPass("Writer", PassFlags::Raster).Write(imported);
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
        resource = producer.Write(resource);
        forced.Read(resource);
        unused.Read(resource);
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
        shared = producer.Write(shared);
        live.Read(shared);
        output = live.Write(output);
        unused.Read(shared);
        graph.ExportTexture(output);
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
        graph.AddPass("Unused", PassFlags::Raster).Write(resource);
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
        resource = writer.Write(resource);
        auto reader = graph.AddPass("Reader", PassFlags::Raster);
        reader.Read(resource);
        auto overwrite = graph.AddPass("Overwrite", PassFlags::Raster);
        resource = overwrite.Write(resource);
        TextureHandle output = MakeTexture(graph, "Output");
        auto consumer = graph.AddPass("Consumer", PassFlags::Raster);
        consumer.Read(resource);
        output = consumer.Write(output);
        graph.ExportTexture(output);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && LiveOrderIs(result, {0, 2, 3}) &&
            HasCull(result, 3, false, CullReason::RootOutput) &&
            HasCull(result, 2, false, CullReason::Producer) &&
            HasCull(result, 0, false, CullReason::Producer) &&
            HasCull(result, 1, true, CullReason::UnusedLeaf),
            "Exported overwrite keeps the previous writer via WAW and culls the unused previous reader");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Unused", PassFlags::Raster).Write(resource);
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
        graph.AddPass("Invalid", PassFlags::Raster).Read(resource);
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
        first = passA.Write(first);
        second = passB.Write(second);
        passA.Read(second);
        passB.Read(first);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && result.HasCycle() && result.GetPassCullStates().empty() &&
            result.GetLivePassOrder().empty() && result.Dump().find("cull: skipped") != std::string::npos &&
            result.Dump().find("lifetime: skipped") != std::string::npos,
            "Cycles skip cull results");
    }
    return failures;
}
