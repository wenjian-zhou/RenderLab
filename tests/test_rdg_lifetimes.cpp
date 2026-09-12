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

    const VersionLifetime* FindVersion(
        std::span<const VersionLifetime> lives, uint32_t resourceIndex, uint32_t version)
    {
        for (const VersionLifetime& lifetime : lives)
        {
            if (lifetime.resourceIndex == resourceIndex && lifetime.version == version)
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
        Check(result.IsSuccess() && result.GetResourceLifetimes().empty() && result.GetVersionLifetimes().empty() &&
            result.Dump().find("lifetime:") != std::string::npos,
            "Empty graph has no lifetimes");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        auto consumer = graph.AddPass("Consumer", PassFlags::NeverCull);
        auto producer = graph.AddPass("Producer", PassFlags::Raster);
        resource = producer.Write(resource);
        consumer.Read(resource);
        const CompileResult result = GraphCompiler::Compile(graph);
        const VersionLifetime* version = FindVersion(result.GetVersionLifetimes(), 0, 1);
        const ResourceLifetime* lifetime = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && version != nullptr && version->firstPass == 1 && version->lastPass == 0,
            "Version first/last are producer then consumer pass indices");
        Check(lifetime != nullptr && lifetime->firstPass == 1 && lifetime->lastPass == 0 && !lifetime->exported,
            "Resource interval uses pass indices even when first is numerically greater than last");
        Check(FindVersion(result.GetVersionLifetimes(), 0, 0) == nullptr,
            "Unproduced internal v0 is not emitted");
    }
    {
        GraphBuilder graph;
        TextureHandle a = MakeTexture(graph, "A");
        TextureHandle b = MakeTexture(graph, "B");
        auto first = graph.AddPass("First", PassFlags::NeverCull);
        auto second = graph.AddPass("Second", PassFlags::NeverCull);
        a = first.Write(a);
        second.Read(a);
        b = second.Write(b);
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
        graph.AddPass("First", PassFlags::NeverCull).Write(a);
        graph.AddPass("Second", PassFlags::NeverCull).Write(b);
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
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::Rgba8Unorm});
        graph.AddPass("Writer", PassFlags::NeverCull).Write(other);
        graph.AddPass("Reader", PassFlags::NeverCull).Read(imported);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 1);
        const VersionLifetime* importedV0 = FindVersion(result.GetVersionLifetimes(), 1, 0);
        Check(result.IsSuccess() && importedLife != nullptr && importedLife->imported &&
            importedLife->firstPass == 0 && importedLife->lastPass == 1 &&
            importedV0 != nullptr && importedV0->firstPass == 1 && importedV0->lastPass == 1,
            "Imported resource is live from graph entry through last live access");
    }
    {
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::Rgba8Unorm});
        TextureHandle a = MakeTexture(graph, "A");
        TextureHandle b = MakeTexture(graph, "B");
        graph.AddPass("First", PassFlags::NeverCull).Write(a);
        graph.AddPass("Second", PassFlags::NeverCull).Write(b);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 0);
        const VersionLifetime* importedV0 = FindVersion(result.GetVersionLifetimes(), 0, 0);
        Check(result.IsSuccess() && importedLife != nullptr && importedLife->imported &&
            importedLife->firstPass == 0 && importedLife->lastPass == 0 &&
            importedV0 != nullptr && importedV0->firstPass == 0 && importedV0->lastPass == 0,
            "Unused imported is live at graph entry and is not extended to the last live pass");
    }
    {
        GraphBuilder graph;
        TextureHandle output = MakeTexture(graph, "Output");
        TextureHandle other = MakeTexture(graph, "Other");
        output = graph.AddPass("Writer", PassFlags::Raster).Write(output);
        graph.ExportTexture(output);
        graph.AddPass("Later", PassFlags::NeverCull).Write(other);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* outputLife = FindResource(result.GetResourceLifetimes(), 0);
        const VersionLifetime* outputV1 = FindVersion(result.GetVersionLifetimes(), 0, 1);
        const ResourceLifetime* otherLife = FindResource(result.GetResourceLifetimes(), 1);
        Check(result.IsSuccess() && outputLife != nullptr && outputLife->exported &&
            outputLife->firstPass == 0 && outputLife->lastPass == 1 &&
            outputV1 != nullptr && outputV1->firstPass == 0 && outputV1->lastPass == 0 &&
            otherLife != nullptr && OverlapsOnLiveSchedule(result, *outputLife, *otherLife),
            "Exported last use extends through the last live pass");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Unused", PassFlags::Raster).Write(resource);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && result.GetResourceLifetimes().empty() && result.GetVersionLifetimes().empty() &&
            !result.GetPassCullStates().empty() && result.GetErrors().size() == 1 &&
            result.GetErrors()[0].category == ErrorCategory::ZeroUseAllocation &&
            result.Dump().find("lifetime: skipped") != std::string::npos,
            "Culled-only created resources are zero-use compile failures");
    }
    {
        GraphBuilder graph;
        TextureHandle output = MakeTexture(graph, "Output");
        TextureHandle unused = MakeTexture(graph, "Unused");
        output = graph.AddPass("Live", PassFlags::Raster).Write(output);
        graph.ExportTexture(output);
        graph.AddPass("Dead", PassFlags::Raster).Write(unused);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && result.GetResourceLifetimes().empty() && result.GetVersionLifetimes().empty() &&
            result.GetPassCullStates().size() == 2 && result.GetLivePassOrder().size() == 1 &&
            result.GetErrors().size() == 1 && result.GetErrors()[0].category == ErrorCategory::ZeroUseAllocation &&
            result.GetErrors()[0].resourceName == "Unused",
            "A mixed live export and unused create fails without partial lifetimes");
    }
    {
        GraphBuilder graph;
        graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::Rgba8Unorm});
        graph.AddPass("Forced", PassFlags::NeverCull);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && importedLife != nullptr && importedLife->imported &&
            importedLife->firstPass == 0 && importedLife->lastPass == 0,
            "Imported unused is not a zero-use error");
    }
    {
        GraphBuilder graph;
        graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::Rgba8Unorm});
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 0);
        const VersionLifetime* importedV0 = FindVersion(result.GetVersionLifetimes(), 0, 0);
        Check(result.IsSuccess() && importedLife != nullptr && importedLife->imported &&
            importedLife->firstPass == Error::kNoPass && importedLife->lastPass == Error::kNoPass &&
            importedV0 != nullptr && importedV0->firstPass == Error::kNoPass &&
            importedV0->lastPass == Error::kNoPass &&
            result.Dump().find("first=none") != std::string::npos,
            "Imported with an empty live schedule uses none for both ends");
    }
    {
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::Rgba8Unorm});
        TextureHandle a = MakeTexture(graph, "A");
        TextureHandle b = MakeTexture(graph, "B");
        graph.ExportTexture(imported);
        graph.AddPass("First", PassFlags::NeverCull).Write(a);
        graph.AddPass("Second", PassFlags::NeverCull).Write(b);
        const CompileResult result = GraphCompiler::Compile(graph);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && importedLife != nullptr && importedLife->imported && importedLife->exported &&
            importedLife->firstPass == 0 && importedLife->lastPass == 1,
            "Imported plus exported uses graph entry for first and last live pass for last");
    }
    {
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::Rgba8Unorm});
        graph.AddPass("Writer", PassFlags::Raster).Write(imported);
        const CompileResult result = GraphCompiler::Compile(graph);
        const VersionLifetime* importedV0 = FindVersion(result.GetVersionLifetimes(), 0, 0);
        const VersionLifetime* importedV1 = FindVersion(result.GetVersionLifetimes(), 0, 1);
        const ResourceLifetime* importedLife = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && importedV0 != nullptr && importedV0->firstPass == 0 && importedV0->lastPass == 0 &&
            importedV1 != nullptr && importedV1->firstPass == 0 && importedV1->lastPass == 0 &&
            importedLife != nullptr && importedLife->imported && importedLife->firstPass == 0 &&
            importedLife->lastPass == 0,
            "Overwritten imported v0 is pinned at graph entry");
    }
    {
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture(TextureDesc{"Imported", 8, 8, Format::Rgba8Unorm});
        graph.AddPass("Reader", PassFlags::NeverCull).Read(imported);
        const CompileResult result = GraphCompiler::Compile(graph);
        const VersionLifetime* version = FindVersion(result.GetVersionLifetimes(), 0, 0);
        const ResourceLifetime* lifetime = FindResource(result.GetResourceLifetimes(), 0);
        Check(result.IsSuccess() && version != nullptr && version->firstPass == 0 && version->lastPass == 0 &&
            lifetime != nullptr && lifetime->imported && lifetime->firstPass == 0 && lifetime->lastPass == 0,
            "Read-only imported v0 still gets first/last from the live read");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Unused", PassFlags::Raster).Write(resource);
        const CompileResult result = GraphCompiler::Compile(graph, CompileOptions{.disableCulling = true});
        const ResourceLifetime* lifetime = FindResource(result.GetResourceLifetimes(), 0);
        const VersionLifetime* version = FindVersion(result.GetVersionLifetimes(), 0, 1);
        Check(result.IsSuccess() && lifetime != nullptr && lifetime->firstPass == 0 && lifetime->lastPass == 0 &&
            version != nullptr && version->firstPass == 0 && version->lastPass == 0,
            "Disabled culling treats an unused writer as a live use");
    }
    {
        GraphBuilder graph;
        TextureHandle output = MakeTexture(graph, "Output");
        output = graph.AddPass("Writer", PassFlags::Raster).Write(output);
        graph.ExportTexture(output);
        const CompileResult first = GraphCompiler::Compile(graph);
        Check(first.Dump() == GraphCompiler::Compile(graph).Dump() &&
            first.Dump().find("lifetime:") != std::string::npos &&
            first.Dump().find("exported") != std::string::npos,
            "Lifetime dump is deterministic");
    }
    {
        GraphBuilder graph;
        TextureHandle resource = MakeTexture(graph);
        graph.AddPass("Invalid", PassFlags::Raster).Read(resource);
        const CompileResult result = GraphCompiler::Compile(graph);
        Check(!result.IsSuccess() && result.GetResourceLifetimes().empty() && result.GetVersionLifetimes().empty() &&
            result.Dump().find("lifetime: skipped") != std::string::npos,
            "Builder errors skip lifetime results");
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
        Check(!result.IsSuccess() && result.HasCycle() && result.GetResourceLifetimes().empty() &&
            result.GetVersionLifetimes().empty() && result.Dump().find("lifetime: skipped") != std::string::npos,
            "Cycles skip lifetime results");
    }
    return failures;
}
