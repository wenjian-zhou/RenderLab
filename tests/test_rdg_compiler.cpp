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
        return graph.CreateTexture(TextureDesc{name, 8, 8, Format::RGBA8Unorm});
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

    bool HasSingleResource(const DependencyEdge* edge, uint32_t resourceIndex)
    {
        return edge != nullptr && edge->resources.size() == 1 && edge->resources[0].resourceIndex == resourceIndex;
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
        auto producer = graph.AddPass("Producer", PassFlags::Raster);
        auto consumer = graph.AddPass("Consumer", PassFlags::Raster);
        producer.Use(resource, Access::RenderTarget);
        consumer.Use(resource, Access::ShaderResource);
        graph.ExportTexture(resource, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && result.GetPassOrder().size() == 2 && result.GetPassOrder()[0] == 0 &&
            result.GetPassOrder()[1] == 1,
            "Pass order is AddPass order");
        Check(HasSingleResource(FindEdge(result.GetEdges(), 0, 1), 0),
            "Last-producer edge records the writer and the later reader");
        Check(result.Dump().find("cycle:") == std::string::npos, "Dump has no cycle section");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        auto writer = graph.AddPass("Writer", PassFlags::Raster);
        writer.Use(resource, Access::RenderTarget);
        auto first = graph.AddPass("First", PassFlags::Raster);
        auto second = graph.AddPass("Second", PassFlags::Raster);
        first.Use(resource, Access::ShaderResource);
        second.Use(resource, Access::ShaderResource);
        auto overwrite = graph.AddPass("Overwrite", PassFlags::Raster);
        overwrite.Use(resource, Access::RenderTarget);
        graph.AddPass("Independent", PassFlags::Raster);
        graph.ExportTexture(resource, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        const auto edges = result.GetEdges();
        Check(result.IsSuccess() && edges.size() == 2, "Readers link to the last earlier writer only");
        Check(HasSingleResource(FindEdge(edges, 0, 1), 0) && HasSingleResource(FindEdge(edges, 0, 2), 0),
            "Each later reader gets an edge from the writer");
        Check(FindEdge(edges, 0, 3) == nullptr && FindEdge(edges, 1, 3) == nullptr &&
            FindEdge(edges, 2, 3) == nullptr,
            "A later write has no edge from the previous writer or its readers");
        Check(result.GetPassOrder().size() == 5 && result.GetPassOrder()[0] == 0 && result.GetPassOrder()[4] == 4,
            "Every pass stays in insertion order");
        Check(result.Dump() == GraphCompiler::Compile(graph).Dump(), "Compiler dump is deterministic");
    }
    {
        GraphBuilder graph;
        TextureHandle first = MakeTexture(graph, "First");
        TextureHandle second = MakeTexture(graph, "Second");
        auto producer = graph.AddPass("Producer", PassFlags::Raster);
        auto consumer = graph.AddPass("Consumer", PassFlags::Raster);
        producer.Use(first, Access::RenderTarget);
        producer.Use(second, Access::RenderTarget);
        consumer.Use(first, Access::ShaderResource);
        consumer.Use(second, Access::ShaderResource);
        graph.ExportTexture(first, Access::RenderTarget);
        graph.ExportTexture(second, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && result.GetEdges().size() == 1 && result.GetEdges()[0].fromPass == 0 &&
            result.GetEdges()[0].toPass == 1 && result.GetEdges()[0].resources.size() == 2 &&
            result.GetEdges()[0].resources[0].resourceIndex == 0 &&
            result.GetEdges()[0].resources[1].resourceIndex == 1,
            "Resources on one producer/consumer pair merge and sort by index");
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
        Check(!result.IsSuccess() && result.GetPassOrder().empty() && !result.GetErrors().empty() &&
            result.GetErrors()[0].category == ErrorCategory::ReadBeforeProduce &&
            result.Dump().find("cycle:") == std::string::npos,
            "A read of a later write is ReadBeforeProduce and is not a cycle");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Invalid", PassFlags::Raster).Use(resource, Access::ShaderResource);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && !result.GetErrors().empty() && result.GetPassOrder().empty() &&
            result.GetEdges().empty(),
            "Builder declaration errors short-circuit compilation");
    }
    return failures;
}
