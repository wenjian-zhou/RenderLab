#include "rdg/GraphCompiler.h"
#include "rdg/LightingPresentGraph.h"
#include "rdg/Pass.h"
#include "rdg/exec/AccessPlan.h"
#include "rdg/exec/GraphExecutor.h"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>

using namespace renderlab::rdg;
using namespace renderlab::rdg::exec;

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

    const ResourceBoundary* FindBoundary(const AccessPlan& plan, const char* name)
    {
        for (const ResourceBoundary& resource : plan.resources)
        {
            if (resource.name == name)
            {
                return &resource;
            }
        }
        return nullptr;
    }

    const PassResourceState* FindPassState(
        const AccessPlan& plan, const char* passName, const char* resourceName)
    {
        for (const PassResourceState& state : plan.passes)
        {
            if (state.passName == passName && state.resourceName == resourceName)
            {
                return &state;
            }
        }
        return nullptr;
    }

    const ResourceRestore* FindRestore(const AccessPlan& plan, const char* name)
    {
        for (const ResourceRestore& restore : plan.restores)
        {
            if (restore.name == name)
            {
                return &restore;
            }
        }
        return nullptr;
    }

    void CheckReadTransition(
        const PassResourceState* state, Access before, Access after, const char* message)
    {
        Check(state != nullptr && state->mode == AccessMode::Read && state->before == before &&
                  state->required == after && state->after == after,
            message);
    }

    void CheckWriteTransition(
        const PassResourceState* state, Access before, Access after, const char* message)
    {
        Check(state != nullptr && state->mode == AccessMode::Write && state->before == before &&
                  state->required == after && state->after == after,
            message);
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
}

int RunRdgLightingTests()
{
    std::printf("RenderLab S5.5 RDG lighting-present tests\n");

    {
        GraphBuilder builder;
        const LightingPresentGraph graph =
            BuildLightingPresentGraph(builder, 1280, 720, ToneMapOutput::BackBuffer);
        const CompileResult result = GraphCompiler::Compile(builder);
        Check(builder.GetErrors().empty() && result.IsSuccess(), "lighting-present graph compiles");
        Check(result.GetLivePassOrder().size() == 2, "two live passes");
        Check(result.GetLivePassOrder()[0] == graph.deferredLightingPassIndex, "first live is DeferredLighting");
        Check(result.GetLivePassOrder()[1] == graph.postProcessPassIndex, "second live is PostProcess");
        Check((builder.GetPass(graph.deferredLightingPassIndex).flags & PassFlags::Raster) == PassFlags::Raster,
            "DeferredLighting is Raster");
        Check((builder.GetPass(graph.postProcessPassIndex).flags & PassFlags::Raster) == PassFlags::Raster,
            "PostProcess is Raster");
        Check(builder.GetResourceCount() == 6, "six textures");

        const ResourceRecord& hdr = builder.GetResource(graph.hdrCreated.index);
        Check(hdr.name == "HDRSceneColor" && !hdr.imported && !hdr.exported,
            "HDRSceneColor is Create* not imported");
        Check(graph.hdrWritten.index == graph.hdrCreated.index, "hdrWritten is the same slot");
        Check(graph.hdrWritten.version != graph.hdrCreated.version, "Write minted a new HDR version");

        const ResourceRecord& gbufferA = builder.GetResource(graph.gbufferA.index);
        Check(gbufferA.name == "GBufferA" && gbufferA.imported && !gbufferA.exported, "GBufferA imported-only");
        const ResourceRecord& depth = builder.GetResource(graph.gbufferDepth.index);
        Check(depth.name == "GBufferDepth" && depth.imported && !depth.exported, "GBufferDepth imported-only");
        const ResourceRecord& backBuffer = builder.GetResource(graph.outputImported.index);
        Check(backBuffer.name == "BackBuffer" && backBuffer.imported && backBuffer.exported,
            "BackBuffer imported and exported");

        const std::string dump = result.Dump();
        Check(dump.find("0 \"DeferredLighting\" live producer") != std::string::npos,
            "DeferredLighting is live producer");
        Check(dump.find("2 \"PostProcess\" live root-output") != std::string::npos,
            "PostProcess is live root-output");
        Check(dump.find("texture") != std::string::npos && dump.find("\"HDRSceneColor\"") != std::string::npos,
            "dump names HDRSceneColor");
        Check(dump.find("\"HDRSceneColor\" imported") == std::string::npos, "HDR is not imported in dump");
        Check(dump.find("texture") != std::string::npos &&
                  dump.find("\"BackBuffer\" imported exported") != std::string::npos,
            "dump BackBuffer imported exported");
        Check(dump.find("\"GBufferA\" imported") != std::string::npos, "dump GBufferA imported");
        Check(dump.find("\"GBufferDepth\" imported") != std::string::npos, "dump GBufferDepth imported");

        const PassCullState* debugCull = nullptr;
        for (const PassCullState& state : result.GetPassCullStates())
        {
            if (state.passIndex == graph.lightingDebugPassIndex)
            {
                debugCull = &state;
            }
        }
        Check(debugCull && debugCull->culled && debugCull->reason == CullReason::UnusedLeaf,
            "LightingDebug is culled unused-leaf");
        Check(dump.find("1 \"LightingDebug\" culled unused-leaf") != std::string::npos,
            "dump shows LightingDebug unused-leaf");
        Check((builder.GetPass(graph.lightingDebugPassIndex).flags & PassFlags::NeverCull) == PassFlags::None,
            "LightingDebug is not NeverCull");
    }

    {
        GraphBuilder dumpBuilder;
        const LightingPresentGraph dumpGraph =
            BuildLightingPresentGraph(dumpBuilder, 1280, 720, ToneMapOutput::DumpTarget);
        const CompileResult dumpResult = GraphCompiler::Compile(dumpBuilder);
        Check(dumpResult.IsSuccess() && dumpResult.GetLivePassOrder().size() == 2,
            "dump-target graph two live passes");
        const std::string dumpText = dumpResult.Dump();
        Check(dumpText.find("\"PostProcessColor\"") != std::string::npos, "output named PostProcessColor");
        Check(dumpText.find("\"PostProcessColor\" imported exported") == std::string::npos,
            "PostProcessColor is not exported");
        const ResourceRecord& dumpHdr = dumpBuilder.GetResource(dumpGraph.hdrCreated.index);
        Check(!dumpHdr.imported, "dump-target HDR is still Create*");
    }

    {
        GraphBuilder builder;
        BuildLightingPresentGraph(builder, 1280, 720, ToneMapOutput::BackBuffer);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        Check(executor.GetAccessPlan().Dump() == "access-plan: empty\n", "Dump empty before Plan");
        executor.Plan();
        Check(executor.GetErrors().empty(), "Plan of lighting-present graph succeeds");
        const AccessPlan& plan = executor.GetAccessPlan();

        const ResourceBoundary* hdr = FindBoundary(plan, "HDRSceneColor");
        Check(hdr && !hdr->imported && !hdr->exported && hdr->initial == Access::RenderTarget &&
                  hdr->final == Access::RenderTarget,
            "HDR Create* RT/RT");
        const ResourceBoundary* bb = FindBoundary(plan, "BackBuffer");
        Check(bb && bb->imported && bb->exported && bb->initial == Access::Present &&
                  bb->final == Access::Present,
            "BackBuffer imported+exported Present");
        const ResourceBoundary* gbufferA = FindBoundary(plan, "GBufferA");
        Check(gbufferA && gbufferA->imported && !gbufferA->exported &&
                  gbufferA->initial == Access::RenderTarget && gbufferA->final == Access::RenderTarget,
            "GBufferA imported-only RT/RT");
        const ResourceBoundary* gbufferDepth = FindBoundary(plan, "GBufferDepth");
        Check(gbufferDepth && gbufferDepth->imported &&
                  gbufferDepth->initial == Access::DepthWrite && gbufferDepth->final == Access::DepthWrite,
            "GBufferDepth imported-only DepthWrite");

        CheckWriteTransition(
            FindPassState(plan, "DeferredLighting", "HDRSceneColor"),
            Access::RenderTarget,
            Access::RenderTarget,
            "HDR lighting write stays RT");
        CheckReadTransition(
            FindPassState(plan, "DeferredLighting", "GBufferA"),
            Access::RenderTarget,
            Access::ShaderResource,
            "GBufferA RT to SR");
        CheckReadTransition(
            FindPassState(plan, "DeferredLighting", "GBufferDepth"),
            Access::DepthWrite,
            Access::ShaderResource,
            "GBufferDepth DepthWrite to SR");
        CheckReadTransition(
            FindPassState(plan, "PostProcess", "HDRSceneColor"),
            Access::RenderTarget,
            Access::ShaderResource,
            "HDR RT to SR at tone map");
        CheckWriteTransition(
            FindPassState(plan, "PostProcess", "BackBuffer"),
            Access::Present,
            Access::RenderTarget,
            "BackBuffer Present to RT");

        Check(FindPassState(plan, "LightingDebug", "HDRSceneColor") == nullptr,
            "AccessPlan skips culled LightingDebug");

        const ResourceRestore* hdrR = FindRestore(plan, "HDRSceneColor");
        Check(hdrR && hdrR->from == Access::ShaderResource && hdrR->to == Access::RenderTarget,
            "HDR restore SR to RT");
        const ResourceRestore* aR = FindRestore(plan, "GBufferA");
        Check(aR && aR->from == Access::ShaderResource && aR->to == Access::RenderTarget,
            "GBufferA restore SR to RT");
        const ResourceRestore* depthR = FindRestore(plan, "GBufferDepth");
        Check(depthR && depthR->from == Access::ShaderResource && depthR->to == Access::DepthWrite,
            "GBufferDepth restore SR to DepthWrite");
        const ResourceRestore* bbR = FindRestore(plan, "BackBuffer");
        Check(bbR && bbR->from == Access::RenderTarget && bbR->to == Access::Present,
            "BackBuffer restore RT to Present");
    }

    {
        GraphBuilder builder;
        BuildLightingPresentGraph(builder, 1280, 720, ToneMapOutput::DumpTarget);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Plan();
        Check(executor.GetErrors().empty(), "Plan of dump-target lighting graph succeeds");
        const AccessPlan& plan = executor.GetAccessPlan();
        const ResourceBoundary* out = FindBoundary(plan, "PostProcessColor");
        Check(out && out->imported && !out->exported && out->initial == Access::RenderTarget &&
                  out->final == Access::RenderTarget,
            "dump target imported-only RT");
        Check(FindRestore(plan, "PostProcessColor") == nullptr, "dump target needs no restore");
        Check(FindRestore(plan, "HDRSceneColor") != nullptr, "HDR still restores SR to RT");
    }

    {
        GraphBuilder builder;
        const LightingPresentGraph graph =
            BuildLightingPresentGraph(builder, 1280, 720, ToneMapOutput::BackBuffer);
        int aNative = 1;
        int bNative = 2;
        int cNative = 3;
        int depthNative = 4;
        int bbNative = 5;
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.RegisterImport(graph.gbufferA, PhysicalTexture{&aNative, "GBufferA"});
        executor.RegisterImport(graph.gbufferB, PhysicalTexture{&bNative, "GBufferB"});
        executor.RegisterImport(graph.gbufferC, PhysicalTexture{&cNative, "GBufferC"});
        executor.RegisterImport(graph.gbufferDepth, PhysicalTexture{&depthNative, "GBufferDepth"});
        executor.RegisterImport(graph.outputImported, PhysicalTexture{&bbNative, "BackBuffer"});
        executor.Allocate();
        Check(executor.GetErrors().empty(), "Allocate without SetDevice succeeds");
        Check(executor.GetAllocationStats().textureCount == 1, "Allocate one HDR texture");
        Check(executor.GetAllocationStats().bufferCount == 0, "Allocate no buffers");
        Check(executor.GetAllocationStats().estimatedBytes == 1280ull * 720ull * 8ull, "HDR 8 bpp");
        executor.Plan();
        const PhysicalTexture* lightingHdr = nullptr;
        const PhysicalTexture* lightingA = nullptr;
        executor.ExecutePass(graph.deferredLightingPassIndex, [&](PassContext& ctx) {
            lightingA = ctx.GetTexture(graph.gbufferA);
            lightingHdr = ctx.GetTexture(graph.hdrWritten);
            Check(ctx.GetTexture(graph.hdrCreated) == nullptr, "Create* v0 on lighting write is null");
        });
        Check(lightingA && lightingA->native == &aNative, "lighting resolves GBufferA");
        Check(lightingHdr && lightingHdr->native != nullptr && lightingHdr->debugName == "HDRSceneColor",
            "lighting resolves Allocate HDR");
        const PhysicalTexture* postHdr = nullptr;
        const PhysicalTexture* postOut = nullptr;
        executor.ExecutePass(graph.postProcessPassIndex, [&](PassContext& ctx) {
            postHdr = ctx.GetTexture(graph.hdrWritten);
            postOut = ctx.GetTexture(graph.outputWritten);
        });
        Check(postHdr && lightingHdr && postHdr->native == lightingHdr->native,
            "tone map reads same HDR native");
        Check(postOut && postOut->native == &bbNative, "PostProcess resolves written BackBuffer");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::SupersededUse),
            "SupersededUse on hdrCreated v0");
    }

    {
        GraphBuilder builder;
        const LightingPresentGraph graph =
            BuildLightingPresentGraph(builder, 1280, 720, ToneMapOutput::BackBuffer);
        int hdrNative = 6;
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.RegisterImport(graph.hdrCreated, PhysicalTexture{&hdrNative, "HDRSceneColor"});
        Check(HasCategory(executor.GetErrors(), ErrorCategory::IncompatibleAccess),
            "RegisterImport of Create* HDR is IncompatibleAccess");
    }

    {
        GraphBuilder builder;
        const LightingPresentGraph graph =
            BuildLightingPresentGraph(builder, 1280, 720, ToneMapOutput::BackBuffer);
        int bNative = 7;
        int cNative = 8;
        int depthNative = 9;
        int bbNative = 10;
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.RegisterImport(graph.gbufferB, PhysicalTexture{&bNative, "GBufferB"});
        executor.RegisterImport(graph.gbufferC, PhysicalTexture{&cNative, "GBufferC"});
        executor.RegisterImport(graph.gbufferDepth, PhysicalTexture{&depthNative, "GBufferDepth"});
        executor.RegisterImport(graph.outputImported, PhysicalTexture{&bbNative, "BackBuffer"});
        executor.Allocate();
        executor.ExecutePass(graph.deferredLightingPassIndex, [&](PassContext& ctx) {
            Check(ctx.GetTexture(graph.gbufferA) == nullptr, "unregistered GBufferA is null");
        });
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnregisteredImport),
            "UnregisteredImport on GBufferA");
    }

    {
        GraphBuilder builder;
        const LightingPresentGraph graph =
            BuildLightingPresentGraph(builder, 1280, 720, ToneMapOutput::BackBuffer);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        bool debugCalled = false;
        executor.ExecutePass(graph.lightingDebugPassIndex, [&](PassContext&) { debugCalled = true; });
        Check(!debugCalled, "ExecutePass does not invoke culled LightingDebug");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::InvalidPass),
            "ExecutePass of culled LightingDebug is InvalidPass");
    }

    {
        GraphBuilder builder;
        const LightingPresentGraph graph =
            BuildLightingPresentGraph(builder, 1280, 720, ToneMapOutput::BackBuffer);
        int aNative = 11;
        int bNative = 12;
        int cNative = 13;
        int depthNative = 14;
        int bbNative = 15;
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.RegisterImport(graph.gbufferA, PhysicalTexture{&aNative, "GBufferA"});
        executor.RegisterImport(graph.gbufferB, PhysicalTexture{&bNative, "GBufferB"});
        executor.RegisterImport(graph.gbufferC, PhysicalTexture{&cNative, "GBufferC"});
        executor.RegisterImport(graph.gbufferDepth, PhysicalTexture{&depthNative, "GBufferDepth"});
        executor.RegisterImport(graph.outputImported, PhysicalTexture{&bbNative, "BackBuffer"});
        executor.Allocate();
        const PhysicalTexture* lightingHdr = nullptr;
        executor.ExecutePass(graph.deferredLightingPassIndex, [&](PassContext& ctx) {
            lightingHdr = ctx.GetTexture(graph.hdrWritten);
        });
        Check(lightingHdr && lightingHdr->native != nullptr, "ExecutePass without Plan still resolves HDR");
        Check(executor.GetAccessPlan().Dump() == "access-plan: empty\n", "Plan-optional leaves dump empty");
    }

    return g_failures;
}
