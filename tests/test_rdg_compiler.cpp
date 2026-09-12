#include "rdg/GraphCompiler.h"

#include <cstdio>
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

    const DependencyEdge* FindEdge(std::span<const DependencyEdge> edges, uint32_t fromPass, uint32_t toPass)
    {
        for (const DependencyEdge& edge : edges)
        {
            if (edge.fromPass == fromPass && edge.toPass == toPass)
            {
                return &edge;
            }
        }
        return nullptr;
    }

    bool HasSingleReason(const DependencyEdge* edge, DependencyType type, uint32_t resourceIndex)
    {
        return edge != nullptr && edge->reasons.size() == 1 && edge->reasons[0].type == type &&
            edge->reasons[0].resourceIndex == resourceIndex;
    }
}

int RunRdgCompilerTests()
{
    std::printf("RenderLab S4.3 compiler tests\n");
    {
        GraphBuilder graph;
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && result.GetPassOrder().empty() && result.GetEdges().empty(),
            "Empty graph compiles successfully");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        auto consumer = graph.AddPass("Consumer", PassFlags::Raster);
        auto producer = graph.AddPass("Producer", PassFlags::Raster);
        resource = producer.Write(resource);
        consumer.Read(resource);
        graph.ExportTexture(resource);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && result.GetPassOrder().size() == 2 && result.GetPassOrder()[0] == 1 &&
            result.GetPassOrder()[1] == 0,
            "Compiler reorders consumer before producer declaration order");
        Check(result.GetEdges().size() == 1 && result.GetEdges()[0].fromPass == 1 && result.GetEdges()[0].toPass == 0 &&
            result.GetEdges()[0].reasons.size() == 1 && result.GetEdges()[0].reasons[0].type == DependencyType::RAW,
            "RAW edge records producer and reader");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        auto writer = graph.AddPass("Writer", PassFlags::Raster);
        resource = writer.Write(resource);
        auto first = graph.AddPass("First", PassFlags::Raster);
        auto second = graph.AddPass("Second", PassFlags::Raster);
        first.Read(resource);
        second.Read(resource);
        auto overwrite = graph.AddPass("Overwrite", PassFlags::Raster);
        resource = overwrite.Write(resource);
        graph.AddPass("Independent", PassFlags::Raster);
        graph.ExportTexture(resource);
        const CompileResult result = GraphCompiler::Compile(graph);
        const auto edges = result.GetEdges();
        Check(result.IsSuccess() && edges.size() == 5, "RAW, WAR, and WAW edges are retained");
        Check(HasSingleReason(FindEdge(edges, 0, 1), DependencyType::RAW, 0) &&
            HasSingleReason(FindEdge(edges, 0, 2), DependencyType::RAW, 0),
            "RAW edges run from producer to each reader");
        Check(HasSingleReason(FindEdge(edges, 1, 3), DependencyType::WAR, 0) &&
            HasSingleReason(FindEdge(edges, 2, 3), DependencyType::WAR, 0),
            "WAR edges run from each previous reader to the overwrite");
        Check(HasSingleReason(FindEdge(edges, 0, 3), DependencyType::WAW, 0),
            "WAW edge runs from previous producer to overwrite");
        Check(result.GetPassOrder().size() == 5 && result.GetPassOrder()[0] == 0 && result.GetPassOrder()[1] == 1 &&
            result.GetPassOrder()[2] == 2 && result.GetPassOrder()[3] == 3 && result.GetPassOrder()[4] == 4,
            "All passes participate in a stable min-index topological order");
        Check(result.Dump() == GraphCompiler::Compile(graph).Dump(), "Compiler dump is deterministic");
    }
    {
        GraphBuilder graph;
        TextureHandle first = MakeTexture(graph, "First");
        TextureHandle second = MakeTexture(graph, "Second");
        auto producer = graph.AddPass("Producer", PassFlags::Raster);
        auto consumer = graph.AddPass("Consumer", PassFlags::Raster);
        first = producer.Write(first);
        second = producer.Write(second);
        consumer.Read(first);
        consumer.Read(second);
        graph.ExportTexture(first);
        graph.ExportTexture(second);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && result.GetEdges().size() == 1 && result.GetEdges()[0].fromPass == 0 &&
            result.GetEdges()[0].toPass == 1 && result.GetEdges()[0].reasons.size() == 2 &&
            result.GetEdges()[0].reasons[0].type == DependencyType::RAW &&
            result.GetEdges()[0].reasons[0].resourceIndex == 0 &&
            result.GetEdges()[0].reasons[1].type == DependencyType::RAW &&
            result.GetEdges()[0].reasons[1].resourceIndex == 1,
            "Multiple reasons on the same pass pair merge into one edge");
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
        Check(!result.IsSuccess() && result.HasCycle() && result.GetPassOrder().empty() && result.GetErrors().empty() &&
            result.GetEdges().size() == 2,
            "Cycles report a diagnostic without a partial pass order");
        const std::optional<CycleDiagnostic>& cycle = result.GetCycle();
        Check(cycle.has_value() && cycle->passes.size() == 3 && cycle->reasons.size() == 2 &&
            cycle->passes[0].passIndex == 0 && cycle->passes[0].name == "A" &&
            cycle->passes[1].passIndex == 1 && cycle->passes[1].name == "B" &&
            cycle->passes[2].passIndex == 0 && cycle->passes[2].name == "A" &&
            cycle->reasons[0].resourceName == "First" && cycle->reasons[1].resourceName == "Second",
            "Cycle diagnostic names the involved passes and resources");
        Check(result.Dump() == GraphCompiler::Compile(graph).Dump() && result.Dump().find("cycle:") != std::string::npos,
            "Cycle dump is deterministic");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Invalid", PassFlags::Raster).Read(resource);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && !result.GetErrors().empty() && result.GetPassOrder().empty() &&
            result.GetEdges().empty() && !result.HasCycle(),
            "Builder declaration errors short-circuit compilation");
    }
    return failures;
}
