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

    const ResourceLifetime* FindResource(std::span<const ResourceLifetime> lives, uint32_t resourceIndex)
    {
        for (const ResourceLifetime& lifetime : lives)
        {
            if (lifetime.resourceIndex == resourceIndex)
            {
                return &lifetime;
            }
        }
        return nullptr;
    }

    uint32_t LiveSlot(const CompileResult& result, uint32_t passIndex)
    {
        const auto live = result.GetLivePassOrder();
        for (uint32_t slot = 0; slot < live.size(); ++slot)
        {
            if (live[slot] == passIndex)
            {
                return slot;
            }
        }
        return Error::kNoPass;
    }

    bool OverlapsOnLiveSchedule(const CompileResult& result, const ResourceLifetime& a, const ResourceLifetime& b)
    {
        const uint32_t aLo = LiveSlot(result, a.firstPass);
        const uint32_t aHi = LiveSlot(result, a.lastPass);
        const uint32_t bLo = LiveSlot(result, b.firstPass);
        const uint32_t bHi = LiveSlot(result, b.lastPass);
        if (aLo == Error::kNoPass || aHi == Error::kNoPass || bLo == Error::kNoPass || bHi == Error::kNoPass)
        {
            return false;
        }
        return aLo <= bHi && bLo <= aHi;
    }
}

int RunRdgLifetimeTests()
{
    std::printf("RenderLab S4.5 lifetime tests\n");
    {
        GraphBuilder graph;
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(result.IsSuccess() && result.GetResourceLifetimes().empty() &&
            result.Dump().find("lifetime:") != std::string::npos,
            "Empty graph has no lifetimes");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        auto producer = graph.AddPass("Producer", PassFlags::Raster);
        auto consumer = graph.AddPass("Consumer", PassFlags::NeverCull);
        producer.Use(resource, Access::RenderTarget);
        consumer.Use(resource, Access::ShaderResource);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* lifetime = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && lifetime != nullptr && lifetime->firstPass == 0 && lifetime->lastPass == 1 &&
            !lifetime->exported,
            "Resource interval runs from the producer pass to the later consumer");
    }
    {
        GraphBuilder graph;
        TextureHandle a = MakeTexture(graph, "A");
        TextureHandle b = MakeTexture(graph, "B");
        auto first = graph.AddPass("First", PassFlags::NeverCull);
        auto second = graph.AddPass("Second", PassFlags::NeverCull);
        first.Use(a, Access::RenderTarget);
        second.Use(a, Access::ShaderResource);
        second.Use(b, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* lifetimeA = FindResource(result.GetResourceLifetimes(), 0);
        const ResourceLifetime* lifetimeB = FindResource(result.GetResourceLifetimes(), 1);
        Check(result.IsSuccess() && lifetimeA != nullptr && lifetimeB != nullptr &&
            lifetimeA->firstPass == 0 && lifetimeA->lastPass == 1 &&
            lifetimeB->firstPass == 1 && lifetimeB->lastPass == 1 &&
            OverlapsOnLiveSchedule(result, *lifetimeA, *lifetimeB),
            "Shared NeverCull chain produces overlapping live-slot intervals");
    }
    {
        GraphBuilder graph;
        TextureHandle a = MakeTexture(graph, "A");
        TextureHandle b = MakeTexture(graph, "B");
        graph.AddPass("First", PassFlags::NeverCull).Use(a, Access::RenderTarget);
        graph.AddPass("Second", PassFlags::NeverCull).Use(b, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* lifetimeA = FindResource(result.GetResourceLifetimes(), 0);
        const ResourceLifetime* lifetimeB = FindResource(result.GetResourceLifetimes(), 1);
        Check(result.IsSuccess() && lifetimeA != nullptr && lifetimeB != nullptr &&
            lifetimeA->firstPass == 0 && lifetimeA->lastPass == 0 &&
            lifetimeB->firstPass == 1 && lifetimeB->lastPass == 1 &&
            !OverlapsOnLiveSchedule(result, *lifetimeA, *lifetimeB),
            "Independent NeverCull writers do not overlap");
    }
    {
        GraphBuilder graph;
        TextureHandle other = MakeTexture(graph, "Other");
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::RGBA8Unorm});
        graph.AddPass("Writer", PassFlags::NeverCull).Use(other, Access::RenderTarget);
        graph.AddPass("Reader", PassFlags::NeverCull).Use(imported, Access::ShaderResource);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 1);
        Check(result.IsSuccess() && importedLife != nullptr && importedLife->imported &&
            importedLife->firstPass == 0 && importedLife->lastPass == 1,
            "Imported resource is live from graph entry through last live access");
    }
    {
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::RGBA8Unorm});
        TextureHandle a = MakeTexture(graph, "A");
        TextureHandle b = MakeTexture(graph, "B");
        graph.AddPass("First", PassFlags::NeverCull).Use(a, Access::RenderTarget);
        graph.AddPass("Second", PassFlags::NeverCull).Use(b, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && importedLife != nullptr && importedLife->imported &&
            importedLife->firstPass == 0 && importedLife->lastPass == 0,
            "Unused imported is live at graph entry and is not extended to the last live pass");
    }
    {
        GraphBuilder graph;
        TextureHandle output = MakeTexture(graph, "Output");
        TextureHandle other = MakeTexture(graph, "Other");
        graph.AddPass("Writer", PassFlags::Raster).Use(output, Access::RenderTarget);
        graph.ExportTexture(output, Access::RenderTarget);
        graph.AddPass("Later", PassFlags::NeverCull).Use(other, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* outputLife = FindResource(result.GetResourceLifetimes(), 0);
        const ResourceLifetime* otherLife = FindResource(result.GetResourceLifetimes(), 1);
        Check(result.IsSuccess() && outputLife != nullptr && outputLife->exported &&
            outputLife->firstPass == 0 && outputLife->lastPass == 1 &&
            otherLife != nullptr && OverlapsOnLiveSchedule(result, *outputLife, *otherLife),
            "Exported last use extends through the last live pass");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Unused", PassFlags::Raster).Use(resource, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && result.GetResourceLifetimes().empty() &&
            !result.GetPassCullStates().empty() && result.GetErrors().size() == 1 &&
            result.GetErrors()[0].category == ErrorCategory::ZeroUseAllocation &&
            result.Dump().find("lifetime: skipped") != std::string::npos,
            "Culled-only created resources are zero-use compile failures");
    }
    {
        GraphBuilder graph;
        TextureHandle output = MakeTexture(graph, "Output");
        TextureHandle unused = MakeTexture(graph, "Unused");
        graph.AddPass("Live", PassFlags::Raster).Use(output, Access::RenderTarget);
        graph.ExportTexture(output, Access::RenderTarget);
        graph.AddPass("Dead", PassFlags::Raster).Use(unused, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && result.GetResourceLifetimes().empty() &&
            result.GetPassCullStates().size() == 2 && result.GetLivePassOrder().size() == 1 &&
            result.GetErrors().size() == 1 && result.GetErrors()[0].category == ErrorCategory::ZeroUseAllocation &&
            result.GetErrors()[0].resourceName == "Unused",
            "A mixed live export and unused create fails without partial lifetimes");
    }
    {
        GraphBuilder graph;
        graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::RGBA8Unorm});
        graph.AddPass("Forced", PassFlags::NeverCull);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && importedLife != nullptr && importedLife->imported &&
            importedLife->firstPass == 0 && importedLife->lastPass == 0,
            "Imported unused is not a zero-use error");
    }
    {
        GraphBuilder graph;
        graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::RGBA8Unorm});
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && importedLife != nullptr && importedLife->imported &&
            importedLife->firstPass == Error::kNoPass && importedLife->lastPass == Error::kNoPass &&
            result.Dump().find("first=none") != std::string::npos,
            "Imported with an empty live schedule uses none for both ends");
    }
    {
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::RGBA8Unorm});
        TextureHandle a = MakeTexture(graph, "A");
        TextureHandle b = MakeTexture(graph, "B");
        graph.ExportTexture(imported, Access::RenderTarget);
        graph.AddPass("First", PassFlags::NeverCull).Use(a, Access::RenderTarget);
        graph.AddPass("Second", PassFlags::NeverCull).Use(b, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && importedLife != nullptr && importedLife->imported && importedLife->exported &&
            importedLife->firstPass == 0 && importedLife->lastPass == 1,
            "Imported plus exported uses graph entry for first and last live pass for last");
    }
    {
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::RGBA8Unorm});
        graph.AddPass("Writer", PassFlags::Raster).Use(imported, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && importedLife != nullptr && importedLife->imported &&
            importedLife->firstPass == 0 && importedLife->lastPass == 0,
            "An imported write is one resource interval pinned at that live pass");
    }
    {
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::RGBA8Unorm});
        graph.AddPass("Reader", PassFlags::NeverCull).Use(imported, Access::ShaderResource);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* lifetime = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && lifetime != nullptr && lifetime->imported && lifetime->firstPass == 0 &&
            lifetime->lastPass == 0,
            "Read-only imported still gets first/last from the live read");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Unused", PassFlags::Raster).Use(resource, Access::RenderTarget);
        const CompileResult result = GraphCompiler::Compile(graph, CompileOptions{.disableCulling = true});
        const ResourceLifetime* lifetime = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && lifetime != nullptr && lifetime->firstPass == 0 && lifetime->lastPass == 0,
            "Disabled culling treats an unused writer as a live use");
    }
    {
        GraphBuilder graph;
        TextureHandle output = MakeTexture(graph, "Output");
        graph.AddPass("Writer", PassFlags::Raster).Use(output, Access::RenderTarget);
        graph.ExportTexture(output, Access::RenderTarget);
        const CompileResult first = GraphCompiler::Compile(graph);
        Check(first.Dump() == GraphCompiler::Compile(graph).Dump() &&
            first.Dump().find("lifetime:") != std::string::npos &&
            first.Dump().find("exported") != std::string::npos,
            "Lifetime dump is deterministic");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Invalid", PassFlags::Raster).Use(resource, Access::ShaderResource);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && result.GetResourceLifetimes().empty() &&
            result.Dump().find("lifetime: skipped") != std::string::npos,
            "Builder errors skip lifetime results");
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
            result.GetResourceLifetimes().empty() && result.Dump().find("lifetime: skipped") != std::string::npos &&
            result.Dump().find("cycle:") == std::string::npos,
            "A backward read skips lifetime results");
    }
    return failures;
}
