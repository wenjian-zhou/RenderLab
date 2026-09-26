#include "rdg/GraphCompiler.h"
#include "rdg/LightingPresentGraph.h"
#include "rdg/Pass.h"
#include "rdg/AccessPlan.h"
#include "rdg/GraphExecutor.h"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
#include <utility>

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

    struct LightingExec
    {
        GraphBuilder graph;
        TextureHandle gbufferA;
        TextureHandle gbufferB;
        TextureHandle gbufferC;
        TextureHandle gbufferDepth;
        TextureHandle hdrCreated;
        TextureHandle hdrWritten;
        TextureHandle outputImported;
        TextureHandle outputWritten;
    };

    PassLambda Noop()
    {
        return [](nvrhi::ICommandList*, PassContext&) {};
    }

    void BuildLightingExec(
        LightingExec& built, ToneMapOutput output, PassLambda lighting, PassLambda debug, PassLambda post)
    {
        built.gbufferA = built.graph.ImportTexture({"GBufferA", 1280, 720, Format::SRGBA8Unorm});
        built.gbufferB = built.graph.ImportTexture({"GBufferB", 1280, 720, Format::RGBA16Float});
        built.gbufferC = built.graph.ImportTexture({"GBufferC", 1280, 720, Format::RGBA8Unorm});
        built.gbufferDepth = built.graph.ImportTexture({"GBufferDepth", 1280, 720, Format::D32Float});
        built.hdrCreated = built.graph.CreateTexture({"HDRSceneColor", 1280, 720, Format::RGBA16Float});
        const bool exportOutput = output == ToneMapOutput::BackBuffer;
        const char* outputName = exportOutput ? "BackBuffer" : "PostProcessColor";
        built.outputImported = built.graph.ImportTexture({outputName, 1280, 720, Format::SRGBA8Unorm});

        PassBuilder lightingPass =
            built.graph.AddPass("DeferredLighting", PassFlags::Raster, std::move(lighting));
        lightingPass.Read(built.gbufferA);
        lightingPass.Read(built.gbufferB);
        lightingPass.Read(built.gbufferC);
        lightingPass.Read(built.gbufferDepth);
        built.hdrWritten = lightingPass.Write(built.hdrCreated);

        PassBuilder debugPass = built.graph.AddPass("LightingDebug", PassFlags::Raster, std::move(debug));
        debugPass.Read(built.gbufferA);
        debugPass.Read(built.gbufferB);
        debugPass.Read(built.gbufferC);
        debugPass.Read(built.gbufferDepth);
        debugPass.Read(built.hdrWritten);

        PassBuilder postPass = built.graph.AddPass("PostProcess", PassFlags::Raster, std::move(post));
        postPass.Read(built.hdrWritten);
        built.outputWritten = postPass.Write(built.outputImported);
        if (exportOutput)
        {
            built.graph.ExportTexture(built.outputWritten);
        }
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
        int aNative = 1;
        int bNative = 2;
        int cNative = 3;
        int depthNative = 4;
        int bbNative = 5;
        const PhysicalTexture* lightingHdr = nullptr;
        const PhysicalTexture* lightingA = nullptr;
        const PhysicalTexture* postHdr = nullptr;
        const PhysicalTexture* postOut = nullptr;
        LightingExec built;
        BuildLightingExec(
            built,
            ToneMapOutput::BackBuffer,
            [&](nvrhi::ICommandList*, PassContext& ctx) {
                lightingA = ctx.GetTexture(built.gbufferA);
                lightingHdr = ctx.GetTexture(built.hdrWritten);
                Check(ctx.GetTexture(built.hdrCreated) == nullptr, "Create* v0 on lighting write is null");
            },
            Noop(),
            [&](nvrhi::ICommandList*, PassContext& ctx) {
                postHdr = ctx.GetTexture(built.hdrWritten);
                postOut = ctx.GetTexture(built.outputWritten);
            });
        GraphExecutor executor(built.graph, GraphCompiler::Compile(built.graph));
        executor.RegisterImport(built.gbufferA, PhysicalTexture{&aNative, "GBufferA"});
        executor.RegisterImport(built.gbufferB, PhysicalTexture{&bNative, "GBufferB"});
        executor.RegisterImport(built.gbufferC, PhysicalTexture{&cNative, "GBufferC"});
        executor.RegisterImport(built.gbufferDepth, PhysicalTexture{&depthNative, "GBufferDepth"});
        executor.RegisterImport(built.outputImported, PhysicalTexture{&bbNative, "BackBuffer"});
        executor.Allocate();
        Check(executor.GetErrors().empty(), "Allocate without SetDevice succeeds");
        Check(executor.GetAllocationStats().textureCount == 1, "Allocate one HDR texture");
        Check(executor.GetAllocationStats().bufferCount == 0, "Allocate no buffers");
        Check(executor.GetAllocationStats().estimatedBytes == 1280ull * 720ull * 8ull, "HDR 8 bpp");
        executor.Plan();
        executor.Execute(nullptr);
        Check(lightingA && lightingA->native == &aNative, "lighting resolves GBufferA");
        Check(lightingHdr && lightingHdr->native != nullptr && lightingHdr->debugName == "HDRSceneColor",
            "lighting resolves Allocate HDR");
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
        int bNative = 7;
        int cNative = 8;
        int depthNative = 9;
        int bbNative = 10;
        LightingExec built;
        BuildLightingExec(
            built,
            ToneMapOutput::BackBuffer,
            [&](nvrhi::ICommandList*, PassContext& ctx) {
                Check(ctx.GetTexture(built.gbufferA) == nullptr, "unregistered GBufferA is null");
            },
            Noop(),
            Noop());
        GraphExecutor executor(built.graph, GraphCompiler::Compile(built.graph));
        executor.RegisterImport(built.gbufferB, PhysicalTexture{&bNative, "GBufferB"});
        executor.RegisterImport(built.gbufferC, PhysicalTexture{&cNative, "GBufferC"});
        executor.RegisterImport(built.gbufferDepth, PhysicalTexture{&depthNative, "GBufferDepth"});
        executor.RegisterImport(built.outputImported, PhysicalTexture{&bbNative, "BackBuffer"});
        executor.Allocate();
        executor.Execute(nullptr);
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnregisteredImport),
            "UnregisteredImport on GBufferA");
    }

    {
        bool debugCalled = false;
        LightingExec built;
        BuildLightingExec(
            built,
            ToneMapOutput::BackBuffer,
            Noop(),
            [&](nvrhi::ICommandList*, PassContext&) { debugCalled = true; },
            Noop());
        GraphExecutor executor(built.graph, GraphCompiler::Compile(built.graph));
        executor.Execute(nullptr);
        Check(!debugCalled, "Execute does not invoke culled LightingDebug");
        Check(!HasCategory(executor.GetErrors(), ErrorCategory::InvalidPass),
            "Culled LightingDebug is skipped instead of InvalidPass");
    }

    {
        int aNative = 11;
        int bNative = 12;
        int cNative = 13;
        int depthNative = 14;
        int bbNative = 15;
        const PhysicalTexture* lightingHdr = nullptr;
        LightingExec built;
        BuildLightingExec(
            built,
            ToneMapOutput::BackBuffer,
            [&](nvrhi::ICommandList*, PassContext& ctx) { lightingHdr = ctx.GetTexture(built.hdrWritten); },
            Noop(),
            Noop());
        GraphExecutor executor(built.graph, GraphCompiler::Compile(built.graph));
        executor.RegisterImport(built.gbufferA, PhysicalTexture{&aNative, "GBufferA"});
        executor.RegisterImport(built.gbufferB, PhysicalTexture{&bNative, "GBufferB"});
        executor.RegisterImport(built.gbufferC, PhysicalTexture{&cNative, "GBufferC"});
        executor.RegisterImport(built.gbufferDepth, PhysicalTexture{&depthNative, "GBufferDepth"});
        executor.RegisterImport(built.outputImported, PhysicalTexture{&bbNative, "BackBuffer"});
        executor.Allocate();
        executor.Execute(nullptr);
        Check(lightingHdr && lightingHdr->native != nullptr, "Execute without Plan still resolves HDR");
        Check(executor.GetAccessPlan().Dump() == "access-plan: empty\n", "Plan-optional leaves dump empty");
    }

    return g_failures;
}
