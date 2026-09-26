#include "rdg/GraphCompiler.h"
#include "rdg/M1ShapedGraph.h"
#include "rdg/RasterFrameGraph.h"
#include "rdg/GraphExecutor.h"

#include <cstdio>
#include <span>
#include <string>

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

    bool HasCategory(std::span<const Error> errors, ErrorCategory category)
    {
        for (const Error& error : errors)
        {
            if (error.category == category)
            {
                return true;
            }
        }
        return false;
    }

    void PrintCompileErrors(const CompileResult& compiled)
    {
        for (const Error& error : compiled.GetErrors())
        {
            std::printf("    compile: %s\n", error.message.c_str());
        }
    }

    struct TextureUse
    {
        TextureHandle handle;
        const PhysicalTexture* physical = nullptr;
    };

    struct BufferUse
    {
        BufferHandle handle;
        const PhysicalBuffer* physical = nullptr;
    };

    bool HasIndexPair(const AllocationStats& stats, uint32_t left, uint32_t right)
    {
        for (const ReusePair& pair : stats.reusePairs)
        {
            if ((pair.ownerIndex == left && pair.aliasIndex == right) ||
                (pair.ownerIndex == right && pair.aliasIndex == left))
            {
                return true;
            }
        }
        return false;
    }

    bool HasName(const AllocationStats& stats, const char* name)
    {
        for (const ReusePair& pair : stats.reusePairs)
        {
            if (pair.ownerName == name || pair.aliasName == name)
            {
                return true;
            }
        }
        return false;
    }

    void CheckDisjointTexturesShareNative()
    {
        GraphBuilder graph;
        TextureHandle createdA = graph.CreateTexture({"First", 8, 4, Format::RGBA8Unorm});
        TextureHandle createdB = graph.CreateTexture({"Second", 8, 4, Format::RGBA8Unorm});
        BufferHandle createdOrder = graph.CreateBuffer({"Order", 4, 1});
        TextureHandle backImported = graph.ImportTexture({"Back", 8, 4, Format::RGBA8Unorm});

        TextureUse first;
        TextureUse second;
        BufferUse order;
        PassBuilder writeFirst = graph.AddPass("WriteFirst", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            first.physical = ctx.GetTexture(first.handle);
        });
        writeFirst.Use(createdA, Access::RenderTarget);
        first.handle = createdA;
        writeFirst.Use(createdOrder, Access::RenderTarget);
        order.handle = createdOrder;

        PassBuilder writeSecond = graph.AddPass("WriteSecond", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            second.physical = ctx.GetTexture(second.handle);
        });
        writeSecond.Use(order.handle, Access::ShaderResource);
        writeSecond.Use(createdB, Access::RenderTarget);
        second.handle = createdB;
        writeSecond.Use(backImported, Access::RenderTarget);
        TextureHandle back = backImported;
        graph.ExportTexture(back, Access::Present);

        const CompileResult compiled = GraphCompiler::Compile(graph);
        PrintCompileErrors(compiled);
        Check(compiled.IsSuccess(), "Disjoint textures compile");
        GraphExecutor executor(graph, compiled);
        executor.Allocate();
        executor.Execute(nullptr);

        const AllocationStats stats = executor.GetAllocationStats();
        const uint64_t textureBytes = 8ull * 4ull * 4ull;
        Check(executor.GetErrors().empty(), "Disjoint textures allocate without errors");
        Check(first.physical != nullptr && second.physical != nullptr, "Disjoint textures resolve");
        Check(first.physical != nullptr && second.physical != nullptr &&
                  first.physical->native == second.physical->native,
              "Non-overlapping exact textures share one native");
        Check(first.physical != nullptr && first.physical->debugName == "First", "Owner keeps its debug name");
        Check(second.physical != nullptr && second.physical->debugName == "Second", "Alias keeps its debug name");
        Check(stats.textureCount == 1 && stats.logicalTextureCount == 2, "Two logical textures, one physical");
        Check(stats.reusePairs.size() == 1, "One reuse pair");
        Check(!stats.reusePairs.empty() && stats.reusePairs[0].ownerIndex == first.handle.index &&
                  stats.reusePairs[0].aliasIndex == second.handle.index &&
                  stats.reusePairs[0].ownerName == "First" && stats.reusePairs[0].aliasName == "Second",
              "Pair points at the earlier owner");
        Check(stats.savedBytes == textureBytes, "Saved bytes equal the aliased texture");
        Check(stats.peakPhysicalBytes == stats.estimatedBytes, "Peak physical bytes are the created total");
        Check(stats.peakLogicalBytes == stats.peakPhysicalBytes + stats.savedBytes, "Saved bytes are logical minus physical");

        executor.SetTransientReuse(false);
        Check(HasCategory(executor.GetErrors(), ErrorCategory::IncompatibleAccess),
              "SetTransientReuse after Allocate is IncompatibleAccess");
        Check(executor.GetAllocationStats().reusePairs.size() == 1, "Late switch does not drop the reuse pair");
        Check(executor.GetAllocationStats().textureCount == 1, "Late switch does not allocate another texture");
    }

    void CheckSamePassRejectsReuse()
    {
        GraphBuilder graph;
        TextureHandle createdA = graph.CreateTexture({"Left", 8, 4, Format::RGBA8Unorm});
        TextureHandle createdB = graph.CreateTexture({"Right", 8, 4, Format::RGBA8Unorm});
        TextureHandle backImported = graph.ImportTexture({"Back", 8, 4, Format::RGBA8Unorm});
        TextureUse left;
        TextureUse right;
        PassBuilder pass = graph.AddPass("Together", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            left.physical = ctx.GetTexture(left.handle);
            right.physical = ctx.GetTexture(right.handle);
        });
        pass.Use(createdA, Access::RenderTarget);
        left.handle = createdA;
        pass.Use(createdB, Access::RenderTarget);
        right.handle = createdB;
        pass.Use(backImported, Access::RenderTarget);
        graph.ExportTexture(backImported, Access::Present);

        const CompileResult compiled = GraphCompiler::Compile(graph);
        PrintCompileErrors(compiled);
        Check(compiled.IsSuccess(), "Same-pass textures compile");
        GraphExecutor executor(graph, compiled);
        executor.Allocate();
        executor.Execute(nullptr);
        const AllocationStats stats = executor.GetAllocationStats();
        Check(left.physical != nullptr && right.physical != nullptr &&
                  left.physical->native != right.physical->native,
              "Textures used in the same pass stay distinct");
        Check(stats.reusePairs.empty() && stats.savedBytes == 0 && stats.textureCount == 2 &&
                  stats.logicalTextureCount == 2,
              "Same-pass overlap records no reuse");
    }

    void CheckTouchingSlotsRejectReuse()
    {
        GraphBuilder graph;
        TextureHandle createdA = graph.CreateTexture({"EndsHere", 8, 4, Format::RGBA8Unorm});
        TextureHandle createdB = graph.CreateTexture({"StartsHere", 8, 4, Format::RGBA8Unorm});
        TextureHandle backImported = graph.ImportTexture({"Back", 8, 4, Format::RGBA8Unorm});
        TextureUse ends;
        TextureUse starts;
        PassBuilder first = graph.AddPass("End", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            ends.physical = ctx.GetTexture(ends.handle);
        });
        first.Use(createdA, Access::RenderTarget);
        ends.handle = createdA;
        PassBuilder second = graph.AddPass("Start", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            starts.physical = ctx.GetTexture(starts.handle);
        });
        second.Use(ends.handle, Access::ShaderResource);
        second.Use(createdB, Access::RenderTarget);
        starts.handle = createdB;
        second.Use(backImported, Access::RenderTarget);
        graph.ExportTexture(backImported, Access::Present);

        const CompileResult compiled = GraphCompiler::Compile(graph);
        PrintCompileErrors(compiled);
        Check(compiled.IsSuccess(), "Touching lifetimes compile");
        GraphExecutor executor(graph, compiled);
        executor.Allocate();
        executor.Execute(nullptr);
        const AllocationStats stats = executor.GetAllocationStats();
        Check(ends.physical != nullptr && starts.physical != nullptr &&
                  ends.physical->native != starts.physical->native,
              "A resource that ends on the pass another starts does not alias");
        Check(stats.reusePairs.empty() && stats.textureCount == 2, "Touching slots record no reuse pair");
    }

    void CheckIncompatibleDescriptorsStayDistinct()
    {
        GraphBuilder graph;
        TextureHandle createdA = graph.CreateTexture({"Color", 8, 4, Format::RGBA8Unorm});
        TextureHandle createdFormat = graph.CreateTexture({"Wide", 8, 4, Format::RGBA16Float});
        TextureHandle createdSize = graph.CreateTexture({"Tall", 16, 4, Format::RGBA8Unorm});
        BufferHandle createdOrder = graph.CreateBuffer({"Order", 4, 1});
        TextureHandle backImported = graph.ImportTexture({"Back", 8, 4, Format::RGBA8Unorm});
        TextureUse color;
        TextureUse wide;
        TextureUse tall;
        BufferUse order;
        PassBuilder first = graph.AddPass("WriteColor", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            color.physical = ctx.GetTexture(color.handle);
        });
        first.Use(createdA, Access::RenderTarget);
        color.handle = createdA;
        first.Use(createdOrder, Access::RenderTarget);
        order.handle = createdOrder;
        PassBuilder second = graph.AddPass("WriteOthers", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            wide.physical = ctx.GetTexture(wide.handle);
            tall.physical = ctx.GetTexture(tall.handle);
        });
        second.Use(order.handle, Access::ShaderResource);
        second.Use(createdFormat, Access::RenderTarget);
        wide.handle = createdFormat;
        second.Use(createdSize, Access::RenderTarget);
        tall.handle = createdSize;
        second.Use(backImported, Access::RenderTarget);
        graph.ExportTexture(backImported, Access::Present);

        const CompileResult compiled = GraphCompiler::Compile(graph);
        PrintCompileErrors(compiled);
        Check(compiled.IsSuccess(), "Incompatible descriptors compile");
        GraphExecutor executor(graph, compiled);
        executor.Allocate();
        executor.Execute(nullptr);
        const AllocationStats stats = executor.GetAllocationStats();
        Check(color.physical != nullptr && wide.physical != nullptr && tall.physical != nullptr &&
                  color.physical->native != wide.physical->native && color.physical->native != tall.physical->native,
              "Format or size mismatch keeps distinct natives");
        Check(stats.reusePairs.empty() && stats.textureCount == 3 && stats.logicalTextureCount == 3,
              "Incompatible textures are not reuse pairs");
    }

    void CheckBuffersReuseAndRejectStrideOrTexture()
    {
        GraphBuilder graph;
        BufferHandle createdA = graph.CreateBuffer({"BufA", 16, 4});
        BufferHandle createdB = graph.CreateBuffer({"BufB", 16, 4});
        BufferHandle createdStride = graph.CreateBuffer({"Stride", 8, 8});
        TextureHandle createdTex = graph.CreateTexture({"Tex", 8, 4, Format::RGBA8Unorm});
        BufferHandle createdOrder = graph.CreateBuffer({"Order", 4, 1});
        TextureHandle backImported = graph.ImportTexture({"Back", 8, 4, Format::RGBA8Unorm});
        BufferUse bufA;
        BufferUse bufB;
        BufferUse stride;
        TextureUse tex;
        BufferUse order;
        PassBuilder first = graph.AddPass("WriteA", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            bufA.physical = ctx.GetBuffer(bufA.handle);
            tex.physical = ctx.GetTexture(tex.handle);
        });
        first.Use(createdA, Access::RenderTarget);
        bufA.handle = createdA;
        first.Use(createdTex, Access::RenderTarget);
        tex.handle = createdTex;
        first.Use(createdOrder, Access::RenderTarget);
        order.handle = createdOrder;
        PassBuilder second = graph.AddPass("WriteB", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            bufB.physical = ctx.GetBuffer(bufB.handle);
            stride.physical = ctx.GetBuffer(stride.handle);
        });
        second.Use(order.handle, Access::ShaderResource);
        second.Use(createdB, Access::RenderTarget);
        bufB.handle = createdB;
        second.Use(createdStride, Access::RenderTarget);
        stride.handle = createdStride;
        second.Use(backImported, Access::RenderTarget);
        graph.ExportTexture(backImported, Access::Present);

        const CompileResult compiled = GraphCompiler::Compile(graph);
        PrintCompileErrors(compiled);
        Check(compiled.IsSuccess(), "Buffer reuse graph compiles");
        GraphExecutor executor(graph, compiled);
        executor.Allocate();
        executor.Execute(nullptr);
        const AllocationStats stats = executor.GetAllocationStats();
        Check(bufA.physical != nullptr && bufB.physical != nullptr &&
                  bufA.physical->native == bufB.physical->native,
              "Matching buffers share one native");
        Check(stride.physical != nullptr && bufA.physical != nullptr &&
                  stride.physical->native != bufA.physical->native,
              "Equal byte size with a different stride stays distinct");
        Check(tex.physical != nullptr && tex.physical->native != nullptr, "Texture next to a buffer still allocates");
        Check(HasIndexPair(stats, bufA.handle.index, bufB.handle.index), "Matching buffers form one reuse pair");
        Check(!HasIndexPair(stats, bufA.handle.index, stride.handle.index), "Stride mismatch is not a reuse pair");
        Check(!HasName(stats, "Tex"), "A texture is not paired with a buffer");
        Check(stats.bufferCount == 3 && stats.logicalBufferCount == 4, "One buffer alias among four logical buffers");
        Check(stats.savedBytes == 16ull * 4ull, "Saved bytes equal the aliased buffer");
    }

    void CheckImportAndExportAreIneligible()
    {
        GraphBuilder graph;
        TextureHandle ghostImported = graph.ImportTexture({"Ghost", 8, 4, Format::RGBA8Unorm});
        TextureHandle createdInternal = graph.CreateTexture({"Internal", 8, 4, Format::RGBA8Unorm});
        TextureHandle createdEarly = graph.CreateTexture({"Early", 8, 4, Format::RGBA8Unorm});
        TextureHandle createdExported = graph.CreateTexture({"ExportedColor", 8, 4, Format::RGBA8Unorm});
        BufferHandle createdOrder = graph.CreateBuffer({"Order", 4, 1});
        TextureUse internal;
        TextureUse early;
        TextureUse exported;
        BufferUse order;
        PassBuilder first = graph.AddPass("WriteEarly", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            early.physical = ctx.GetTexture(early.handle);
        });
        first.Use(createdEarly, Access::RenderTarget);
        early.handle = createdEarly;
        first.Use(createdOrder, Access::RenderTarget);
        order.handle = createdOrder;
        PassBuilder second = graph.AddPass("WriteLater", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            internal.physical = ctx.GetTexture(internal.handle);
            exported.physical = ctx.GetTexture(exported.handle);
        });
        second.Use(order.handle, Access::ShaderResource);
        second.Use(createdInternal, Access::RenderTarget);
        internal.handle = createdInternal;
        second.Use(createdExported, Access::RenderTarget);
        exported.handle = createdExported;
        graph.ExportTexture(exported.handle, Access::RenderTarget);
        (void)ghostImported;

        const CompileResult compiled = GraphCompiler::Compile(graph);
        PrintCompileErrors(compiled);
        Check(compiled.IsSuccess(), "Import and export eligibility graph compiles");
        GraphExecutor executor(graph, compiled);
        executor.Allocate();
        executor.Execute(nullptr);
        const AllocationStats stats = executor.GetAllocationStats();
        Check(internal.physical != nullptr && early.physical != nullptr &&
                  internal.physical->native == early.physical->native,
              "A later eligible texture reuses the earlier one");
        Check(early.physical != nullptr && exported.physical != nullptr &&
                  early.physical->native != exported.physical->native,
              "Exported Create* keeps its own native when intervals are disjoint");
        Check(!HasName(stats, "Ghost"), "Imported resource is not a reuse participant");
        Check(!HasName(stats, "ExportedColor"), "Exported Create* is not a reuse participant");
        Check(HasIndexPair(stats, early.handle.index, internal.handle.index),
              "Eligible disjoint textures still form a pair");
        Check(stats.textureCount == 2 && stats.logicalTextureCount == 3,
              "Export stays physical while the eligible pair shares one object");
    }

    void CheckReversedPassIndicesStillOverlap()
    {
        GraphBuilder graph;
        TextureHandle createdSpan = graph.CreateTexture({"Span", 8, 4, Format::RGBA8Unorm});
        TextureHandle createdMiddle = graph.CreateTexture({"MiddleTex", 8, 4, Format::RGBA8Unorm});
        BufferHandle createdToken = graph.CreateBuffer({"Token", 4, 1});
        TextureHandle backImported = graph.ImportTexture({"Back", 8, 4, Format::RGBA8Unorm});
        TextureUse span;
        TextureUse middle;
        BufferUse token;

        PassBuilder earlyPass = graph.AddPass("Early", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            span.physical = ctx.GetTexture(span.handle);
        });
        PassBuilder middlePass = graph.AddPass(
            "Middle", PassFlags::Raster | PassFlags::NeverCull, [&](nvrhi::ICommandList*, PassContext& ctx) {
                middle.physical = ctx.GetTexture(middle.handle);
            });
        PassBuilder latePass = graph.AddPass("Late", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            span.physical = ctx.GetTexture(span.handle);
        });
        earlyPass.Use(createdSpan, Access::RenderTarget);
        span.handle = createdSpan;
        earlyPass.Use(createdToken, Access::RenderTarget);
        token.handle = createdToken;
        middlePass.Use(token.handle, Access::ShaderResource);
        middlePass.Use(createdMiddle, Access::RenderTarget);
        middle.handle = createdMiddle;
        latePass.Use(span.handle, Access::ShaderResource);
        latePass.Use(backImported, Access::RenderTarget);
        graph.ExportTexture(backImported, Access::Present);

        const CompileResult compiled = GraphCompiler::Compile(graph);
        PrintCompileErrors(compiled);
        Check(compiled.IsSuccess(), "Overlapping producer graph compiles");
        const uint32_t earlyIndex = earlyPass.PassIndex();
        const uint32_t middleIndex = middlePass.PassIndex();
        const uint32_t lateIndex = latePass.PassIndex();
        Check(compiled.GetLivePassOrder().size() == 3 && compiled.GetLivePassOrder()[0] == earlyIndex &&
                  compiled.GetLivePassOrder()[1] == middleIndex && compiled.GetLivePassOrder()[2] == lateIndex,
              "Live order follows AddPass order");

        bool spanCoversMiddle = false;
        for (const ResourceLifetime& lifetime : compiled.GetResourceLifetimes())
        {
            if (lifetime.resourceIndex == span.handle.index)
            {
                spanCoversMiddle = lifetime.firstPass == earlyIndex && lifetime.lastPass == lateIndex &&
                    lifetime.firstPass < lifetime.lastPass;
            }
        }
        Check(spanCoversMiddle, "Span lifetime covers the early write through the later read");

        GraphExecutor executor(graph, compiled);
        executor.Allocate();
        executor.Execute(nullptr);
        const AllocationStats stats = executor.GetAllocationStats();
        Check(span.physical != nullptr && middle.physical != nullptr &&
                  span.physical->native != middle.physical->native,
              "A resource live across a middle use does not alias that use");
        Check(!HasIndexPair(stats, span.handle.index, middle.handle.index),
              "Overlapping live slots do not form a reuse pair");
    }

    void CheckLowestOwnerAndChain()
    {
        GraphBuilder overlap;
        TextureHandle createdA = overlap.CreateTexture({"A", 8, 4, Format::RGBA8Unorm});
        TextureHandle createdB = overlap.CreateTexture({"B", 8, 4, Format::RGBA8Unorm});
        TextureHandle createdC = overlap.CreateTexture({"C", 8, 4, Format::RGBA8Unorm});
        BufferHandle createdOrder = overlap.CreateBuffer({"Order", 4, 1});
        TextureHandle backImported = overlap.ImportTexture({"Back", 8, 4, Format::RGBA8Unorm});
        TextureUse a;
        TextureUse b;
        TextureUse c;
        BufferUse order;
        PassBuilder both = overlap.AddPass("Both", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            a.physical = ctx.GetTexture(a.handle);
            b.physical = ctx.GetTexture(b.handle);
        });
        both.Use(createdA, Access::RenderTarget);
        a.handle = createdA;
        both.Use(createdB, Access::RenderTarget);
        b.handle = createdB;
        both.Use(createdOrder, Access::RenderTarget);
        order.handle = createdOrder;
        PassBuilder later = overlap.AddPass("Later", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            c.physical = ctx.GetTexture(c.handle);
        });
        later.Use(order.handle, Access::ShaderResource);
        later.Use(createdC, Access::RenderTarget);
        c.handle = createdC;
        later.Use(backImported, Access::RenderTarget);
        overlap.ExportTexture(backImported, Access::Present);

        const CompileResult overlapCompiled = GraphCompiler::Compile(overlap);
        PrintCompileErrors(overlapCompiled);
        Check(overlapCompiled.IsSuccess(), "Lowest-owner graph compiles");
        GraphExecutor overlapExecutor(overlap, overlapCompiled);
        overlapExecutor.Allocate();
        overlapExecutor.Execute(nullptr);
        const AllocationStats overlapStats = overlapExecutor.GetAllocationStats();
        Check(a.physical != nullptr && b.physical != nullptr && c.physical != nullptr &&
                  c.physical->native == a.physical->native && c.physical->native != b.physical->native,
              "The later resource reuses the lower owner index");
        Check(overlapStats.reusePairs.size() == 1 && overlapStats.reusePairs[0].ownerIndex == a.handle.index &&
                  overlapStats.reusePairs[0].aliasIndex == c.handle.index,
              "The single pair names the lower-index owner");
        Check(overlapStats.textureCount == 2 && overlapStats.logicalTextureCount == 3,
              "Overlapping pair plus one alias is two physical textures");

        GraphBuilder chain;
        TextureHandle chainACreated = chain.CreateTexture({"ChainA", 8, 4, Format::RGBA8Unorm});
        TextureHandle chainBCreated = chain.CreateTexture({"ChainB", 8, 4, Format::RGBA8Unorm});
        TextureHandle chainCCreated = chain.CreateTexture({"ChainC", 8, 4, Format::RGBA8Unorm});
        BufferHandle token0Created = chain.CreateBuffer({"Token0", 4, 1});
        BufferHandle token1Created = chain.CreateBuffer({"Token1", 4, 1});
        TextureHandle chainBack = chain.ImportTexture({"Back", 8, 4, Format::RGBA8Unorm});
        TextureUse chainA;
        TextureUse chainB;
        TextureUse chainC;
        BufferUse token0;
        BufferUse token1;
        PassBuilder pass0 = chain.AddPass("Chain0", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            chainA.physical = ctx.GetTexture(chainA.handle);
        });
        pass0.Use(chainACreated, Access::RenderTarget);
        chainA.handle = chainACreated;
        pass0.Use(token0Created, Access::RenderTarget);
        token0.handle = token0Created;
        PassBuilder pass1 = chain.AddPass("Chain1", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            chainB.physical = ctx.GetTexture(chainB.handle);
        });
        pass1.Use(token0.handle, Access::ShaderResource);
        pass1.Use(chainBCreated, Access::RenderTarget);
        chainB.handle = chainBCreated;
        pass1.Use(token1Created, Access::RenderTarget);
        token1.handle = token1Created;
        PassBuilder pass2 = chain.AddPass("Chain2", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            chainC.physical = ctx.GetTexture(chainC.handle);
        });
        pass2.Use(token1.handle, Access::ShaderResource);
        pass2.Use(chainCCreated, Access::RenderTarget);
        chainC.handle = chainCCreated;
        pass2.Use(chainBack, Access::RenderTarget);
        chain.ExportTexture(chainBack, Access::Present);

        const CompileResult chainCompiled = GraphCompiler::Compile(chain);
        PrintCompileErrors(chainCompiled);
        Check(chainCompiled.IsSuccess(), "Alias chain compiles");
        GraphExecutor chainExecutor(chain, chainCompiled);
        chainExecutor.Allocate();
        chainExecutor.Execute(nullptr);
        const AllocationStats chainStats = chainExecutor.GetAllocationStats();
        Check(chainA.physical != nullptr && chainB.physical != nullptr && chainC.physical != nullptr &&
                  chainA.physical->native == chainB.physical->native &&
                  chainA.physical->native == chainC.physical->native,
              "Three sequential textures share the first native");
        Check(chainStats.textureCount == 1 && chainStats.logicalTextureCount == 3, "Chain keeps one physical texture");
        Check(chainStats.reusePairs.size() == 2 && chainStats.reusePairs[0].aliasIndex == chainB.handle.index &&
                  chainStats.reusePairs[1].aliasIndex == chainC.handle.index &&
                  chainStats.reusePairs[0].ownerIndex == chainA.handle.index &&
                  chainStats.reusePairs[1].ownerIndex == chainA.handle.index,
              "Both pairs name the original owner in alias-index order");
        Check(chainStats.savedBytes == 2ull * 8ull * 4ull * 4ull, "Chain saves two texture allocations");
    }

    void CheckDisableSwitchLowersNothingBelowBaseline()
    {
        auto build = [](GraphBuilder& graph, TextureUse& first, TextureUse& second) {
            TextureHandle createdA = graph.CreateTexture({"First", 8, 4, Format::RGBA8Unorm});
            TextureHandle createdB = graph.CreateTexture({"Second", 8, 4, Format::RGBA8Unorm});
            BufferHandle createdOrder = graph.CreateBuffer({"Order", 4, 1});
            TextureHandle backImported = graph.ImportTexture({"Back", 8, 4, Format::RGBA8Unorm});
            BufferUse order;
            PassBuilder writeFirst = graph.AddPass("WriteFirst", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
                first.physical = ctx.GetTexture(first.handle);
            });
            writeFirst.Use(createdA, Access::RenderTarget);
            first.handle = createdA;
            writeFirst.Use(createdOrder, Access::RenderTarget);
            order.handle = createdOrder;
            PassBuilder writeSecond = graph.AddPass("WriteSecond", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
                second.physical = ctx.GetTexture(second.handle);
            });
            writeSecond.Use(order.handle, Access::ShaderResource);
            writeSecond.Use(createdB, Access::RenderTarget);
            second.handle = createdB;
            writeSecond.Use(backImported, Access::RenderTarget);
            graph.ExportTexture(backImported, Access::Present);
        };

        GraphBuilder enabledGraph;
        TextureUse enabledFirst;
        TextureUse enabledSecond;
        build(enabledGraph, enabledFirst, enabledSecond);
        const CompileResult enabledCompiled = GraphCompiler::Compile(enabledGraph);
        Check(enabledCompiled.IsSuccess(), "Enabled reuse graph compiles");
        GraphExecutor enabled(enabledGraph, enabledCompiled);
        enabled.Allocate();
        enabled.Execute(nullptr);

        GraphBuilder disabledGraph;
        TextureUse disabledFirst;
        TextureUse disabledSecond;
        build(disabledGraph, disabledFirst, disabledSecond);
        const CompileResult disabledCompiled = GraphCompiler::Compile(disabledGraph);
        Check(disabledCompiled.IsSuccess(), "Disabled reuse graph compiles");
        GraphExecutor disabled(disabledGraph, disabledCompiled);
        disabled.SetTransientReuse(false);
        disabled.Allocate();
        disabled.Execute(nullptr);

        const AllocationStats on = enabled.GetAllocationStats();
        const AllocationStats off = disabled.GetAllocationStats();
        Check(enabled.GetErrors().empty() && disabled.GetErrors().empty(), "Both reuse modes allocate cleanly");
        Check(enabledFirst.physical != nullptr && enabledSecond.physical != nullptr &&
                  enabledFirst.physical->native == enabledSecond.physical->native,
              "Default reuse shares the native");
        Check(disabledFirst.physical != nullptr && disabledSecond.physical != nullptr &&
                  disabledFirst.physical->native != disabledSecond.physical->native,
              "Disabled reuse keeps two natives");
        Check(off.savedBytes == 0 && off.reusePairs.empty() && off.peakPhysicalBytes == off.peakLogicalBytes,
              "Disabled reuse saves nothing");
        Check(on.savedBytes == 8ull * 4ull * 4ull && on.peakPhysicalBytes <= off.peakPhysicalBytes,
              "Enabled peak physical bytes do not exceed the disabled baseline");
    }

    void CheckProductionGraphsDoNotAlias()
    {
        {
            GraphBuilder builder;
            BuildM1ShapedGraph(builder);
            const CompileResult compiled = GraphCompiler::Compile(builder);
            Check(compiled.IsSuccess(), "M1 shaped graph compiles");
            GraphExecutor executor(builder, compiled);
            executor.Allocate();
            const AllocationStats stats = executor.GetAllocationStats();
            Check(executor.GetErrors().empty() && stats.reusePairs.empty(), "M1 shaped graph has no reuse pairs");
            Check(stats.textureCount == 5 && stats.logicalTextureCount == 5, "M1 still mints five textures");
            Check(stats.estimatedBytes == 1280ull * 720ull * 28ull && stats.savedBytes == 0 &&
                      stats.peakPhysicalBytes == stats.peakLogicalBytes,
                  "M1 byte total is unchanged");
            Check(!HasName(stats, "GBufferB") && !HasName(stats, "HDRSceneColor"),
                  "M1 does not alias GBufferB with HDRSceneColor");
        }

        const auto checkRaster = [](RasterPresent present, uint32_t textureCount, uint64_t bytesPerPixel, const char* label) {
            GraphBuilder builder;
            const RasterFrameGraph frame = BuildRasterFrameGraph(builder, 1280, 720, present);
            const CompileResult compiled = GraphCompiler::Compile(builder);
            Check(compiled.IsSuccess(), label);
            GraphExecutor executor(builder, compiled);
            executor.Allocate();
            const AllocationStats stats = executor.GetAllocationStats();
            Check(executor.GetErrors().empty() && stats.reusePairs.empty(), label);
            Check(stats.textureCount == textureCount && stats.logicalTextureCount == textureCount, label);
            Check(stats.estimatedBytes == 1280ull * 720ull * bytesPerPixel && stats.savedBytes == 0 &&
                      stats.peakPhysicalBytes == stats.estimatedBytes,
                  label);
            if (frame.hasHdr)
            {
                Check(!HasIndexPair(stats, frame.gbufferB.index, frame.hdrCreated.index),
                      "Raster graph does not alias GBufferB with HDRSceneColor");
            }
        };
        checkRaster(RasterPresent::Final, 5, 28, "Final raster graph has no reuse");
        checkRaster(RasterPresent::LightingDebug, 5, 28, "LightingDebug raster graph has no reuse");
        checkRaster(RasterPresent::GBufferDebug, 4, 20, "GBufferDebug raster graph has no reuse");
    }
}

int RunRdgReuseTests()
{
    std::printf("RenderLab S5.7 RDG transient reuse tests\n");
    CheckDisjointTexturesShareNative();
    CheckSamePassRejectsReuse();
    CheckTouchingSlotsRejectReuse();
    CheckIncompatibleDescriptorsStayDistinct();
    CheckBuffersReuseAndRejectStrideOrTexture();
    CheckImportAndExportAreIneligible();
    CheckReversedPassIndicesStillOverlap();
    CheckLowestOwnerAndChain();
    CheckDisableSwitchLowersNothingBelowBaseline();
    CheckProductionGraphsDoNotAlias();
    return g_failures;
}
