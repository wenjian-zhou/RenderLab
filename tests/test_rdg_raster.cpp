#include "rdg/GraphCompiler.h"
#include "rdg/RasterFrameGraph.h"
#include "rdg/AccessPlan.h"
#include "rdg/GraphExecutor.h"

#include <cstdint>
#include <cstdio>
#include <span>
#include <string>
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

    bool LiveNames(const GraphBuilder& builder, const CompileResult& result, const std::vector<const char*>& names)
    {
        if (result.GetLivePassOrder().size() != names.size())
        {
            return false;
        }
        for (size_t index = 0; index < names.size(); ++index)
        {
            const uint32_t passIndex = result.GetLivePassOrder()[index];
            if (builder.GetPass(passIndex).name != names[index])
            {
                return false;
            }
        }
        return true;
    }
}

int RunRdgRasterFrameTests()
{
    std::printf("RenderLab S5.6 RDG raster-frame tests\n");

    {
        GraphBuilder builder;
        const RasterFrameGraph graph =
            BuildRasterFrameGraph(builder, 1280, 720, RasterPresent::Final);
        const CompileResult result = GraphCompiler::Compile(builder);
        Check(builder.GetErrors().empty() && result.IsSuccess(), "Final raster graph compiles");
        Check(LiveNames(builder, result, {"GBuffer", "DeferredLighting", "PostProcess"}),
            "Final live order is GBuffer, DeferredLighting, PostProcess");
        Check(graph.hasHdr, "Final graph has HDR");
        Check(graph.deferredLightingPassIndex != Error::kNoPass, "Final graph has DeferredLighting");
        Check(builder.GetResourceCount() == 6, "Final graph has six textures");

        const ResourceRecord& gbufferA = builder.GetResource(graph.gbufferA.index);
        Check(gbufferA.name == "GBufferA" && !gbufferA.imported && !gbufferA.exported,
            "GBufferA is Create*");
        const ResourceRecord& depth = builder.GetResource(graph.gbufferDepth.index);
        Check(depth.name == "GBufferDepth" && !depth.imported && !depth.exported, "GBufferDepth is Create*");
        const ResourceRecord& hdr = builder.GetResource(graph.hdrCreated.index);
        Check(hdr.name == "HDRSceneColor" && !hdr.imported && !hdr.exported, "HDRSceneColor is Create*");
        Check(graph.hdrWritten.index == graph.hdrCreated.index &&
                  graph.hdrWritten.version != graph.hdrCreated.version,
            "lighting Write minted a new HDR version");
        const ResourceRecord& backBuffer = builder.GetResource(graph.outputImported.index);
        Check(backBuffer.name == "BackBuffer" && backBuffer.imported && backBuffer.exported,
            "BackBuffer imported and exported");

        const std::string dump = result.Dump();
        Check(dump.find("0 \"GBuffer\" live producer") != std::string::npos, "dump GBuffer live producer");
        Check(dump.find("1 \"DeferredLighting\" live producer") != std::string::npos,
            "dump DeferredLighting live producer");
        Check(dump.find("2 \"PostProcess\" live root-output") != std::string::npos,
            "dump PostProcess live root-output");
        Check(dump.find("\"GBufferA\" imported") == std::string::npos, "dump GBufferA is not imported");
        Check(dump.find("\"HDRSceneColor\" imported") == std::string::npos, "dump HDR is not imported");
        Check(dump.find("\"BackBuffer\" imported exported") != std::string::npos,
            "dump BackBuffer imported exported");
    }

    {
        GraphBuilder builder;
        const RasterFrameGraph graph =
            BuildRasterFrameGraph(builder, 1280, 720, RasterPresent::LightingDebug);
        const CompileResult result = GraphCompiler::Compile(builder);
        Check(result.IsSuccess(), "LightingDebug raster graph compiles");
        Check(LiveNames(builder, result, {"GBuffer", "DeferredLighting", "LightingDebug"}),
            "LightingDebug live order keeps DeferredLighting");
        Check(graph.hasHdr && builder.GetResourceCount() == 6, "LightingDebug graph still creates HDR");
        Check(builder.GetPass(graph.presentPassIndex).name == "LightingDebug", "present pass is LightingDebug");
        const std::string dump = result.Dump();
        Check(dump.find("\"PostProcess\"") == std::string::npos, "LightingDebug graph has no PostProcess");
    }

    {
        GraphBuilder builder;
        const RasterFrameGraph graph =
            BuildRasterFrameGraph(builder, 1280, 720, RasterPresent::GBufferDebug);
        const CompileResult result = GraphCompiler::Compile(builder);
        Check(result.IsSuccess(), "GBufferDebug raster graph compiles");
        Check(LiveNames(builder, result, {"GBuffer", "GBufferDebug"}),
            "GBufferDebug live order is GBuffer then GBufferDebug");
        Check(!graph.hasHdr && graph.hdrCreated.IsNull() && graph.hdrWritten.IsNull(),
            "GBufferDebug graph has no HDR handles");
        Check(graph.deferredLightingPassIndex == Error::kNoPass, "GBufferDebug has no DeferredLighting pass");
        Check(builder.GetResourceCount() == 5, "GBufferDebug graph has five textures");
        const std::string dump = result.Dump();
        Check(dump.find("\"HDRSceneColor\"") == std::string::npos, "GBufferDebug dump has no HDR");
        Check(dump.find("\"DeferredLighting\"") == std::string::npos, "GBufferDebug dump has no DeferredLighting");
        Check(dump.find("\"PostProcess\"") == std::string::npos, "GBufferDebug dump has no PostProcess");
        Check(dump.find("1 \"GBufferDebug\" live root-output") != std::string::npos,
            "dump GBufferDebug live root-output");
    }

    {
        GraphBuilder builder;
        BuildRasterFrameGraph(builder, 1280, 720, RasterPresent::Final);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Plan();
        Check(executor.GetErrors().empty(), "Plan of Final raster graph succeeds");
        const AccessPlan& plan = executor.GetAccessPlan();

        const ResourceBoundary* gbufferA = FindBoundary(plan, "GBufferA");
        Check(gbufferA && !gbufferA->imported && !gbufferA->exported &&
                  gbufferA->initial == Access::RenderTarget && gbufferA->final == Access::RenderTarget,
            "GBufferA Create* RT/RT");
        const ResourceBoundary* depth = FindBoundary(plan, "GBufferDepth");
        Check(depth && !depth->imported && depth->initial == Access::DepthWrite &&
                  depth->final == Access::DepthWrite,
            "GBufferDepth Create* DepthWrite");
        const ResourceBoundary* hdr = FindBoundary(plan, "HDRSceneColor");
        Check(hdr && !hdr->imported && !hdr->exported && hdr->initial == Access::RenderTarget &&
                  hdr->final == Access::RenderTarget,
            "HDR Create* RT/RT");
        const ResourceBoundary* backBuffer = FindBoundary(plan, "BackBuffer");
        Check(backBuffer && backBuffer->imported && backBuffer->exported &&
                  backBuffer->initial == Access::Present && backBuffer->final == Access::Present,
            "BackBuffer imported+exported Present");

        CheckWriteTransition(
            FindPassState(plan, "GBuffer", "GBufferA"),
            Access::RenderTarget,
            Access::RenderTarget,
            "GBufferA write stays RT");
        CheckWriteTransition(
            FindPassState(plan, "GBuffer", "GBufferDepth"),
            Access::DepthWrite,
            Access::DepthWrite,
            "GBufferDepth write stays DepthWrite");
        CheckReadTransition(
            FindPassState(plan, "DeferredLighting", "GBufferA"),
            Access::RenderTarget,
            Access::ShaderResource,
            "lighting reads GBufferA RT to SR");
        CheckReadTransition(
            FindPassState(plan, "DeferredLighting", "GBufferDepth"),
            Access::DepthWrite,
            Access::ShaderResource,
            "lighting reads depth DepthWrite to SR");
        CheckWriteTransition(
            FindPassState(plan, "DeferredLighting", "HDRSceneColor"),
            Access::RenderTarget,
            Access::RenderTarget,
            "lighting HDR write stays RT");
        CheckReadTransition(
            FindPassState(plan, "PostProcess", "HDRSceneColor"),
            Access::RenderTarget,
            Access::ShaderResource,
            "tone map reads HDR RT to SR");
        Check(FindPassState(plan, "PostProcess", "GBufferA") == nullptr, "PostProcess does not read GBufferA");
        CheckWriteTransition(
            FindPassState(plan, "PostProcess", "BackBuffer"),
            Access::Present,
            Access::RenderTarget,
            "BackBuffer Present to RT");

        const ResourceRestore* aR = FindRestore(plan, "GBufferA");
        Check(aR && aR->from == Access::ShaderResource && aR->to == Access::RenderTarget,
            "GBufferA restore SR to RT");
        const ResourceRestore* depthR = FindRestore(plan, "GBufferDepth");
        Check(depthR && depthR->from == Access::ShaderResource && depthR->to == Access::DepthWrite,
            "GBufferDepth restore SR to DepthWrite");
        const ResourceRestore* hdrR = FindRestore(plan, "HDRSceneColor");
        Check(hdrR && hdrR->from == Access::ShaderResource && hdrR->to == Access::RenderTarget,
            "HDR restore SR to RT");
        const ResourceRestore* bbR = FindRestore(plan, "BackBuffer");
        Check(bbR && bbR->from == Access::RenderTarget && bbR->to == Access::Present,
            "BackBuffer restore RT to Present");
    }

    {
        GraphBuilder builder;
        BuildRasterFrameGraph(builder, 1280, 720, RasterPresent::LightingDebug);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Plan();
        Check(executor.GetErrors().empty(), "Plan of LightingDebug raster graph succeeds");
        const AccessPlan& plan = executor.GetAccessPlan();
        CheckReadTransition(
            FindPassState(plan, "LightingDebug", "GBufferA"),
            Access::ShaderResource,
            Access::ShaderResource,
            "LightingDebug reads GBufferA already in SR");
        CheckReadTransition(
            FindPassState(plan, "LightingDebug", "HDRSceneColor"),
            Access::RenderTarget,
            Access::ShaderResource,
            "LightingDebug reads HDR RT to SR");
        CheckWriteTransition(
            FindPassState(plan, "LightingDebug", "BackBuffer"),
            Access::Present,
            Access::RenderTarget,
            "LightingDebug writes BackBuffer");
        Check(FindPassState(plan, "PostProcess", "BackBuffer") == nullptr, "no PostProcess state row");
    }

    {
        GraphBuilder builder;
        BuildRasterFrameGraph(builder, 1280, 720, RasterPresent::GBufferDebug);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Plan();
        Check(executor.GetErrors().empty(), "Plan of GBufferDebug raster graph succeeds");
        const AccessPlan& plan = executor.GetAccessPlan();
        Check(FindBoundary(plan, "HDRSceneColor") == nullptr, "GBufferDebug plan has no HDR");
        CheckReadTransition(
            FindPassState(plan, "GBufferDebug", "GBufferA"),
            Access::RenderTarget,
            Access::ShaderResource,
            "GBufferDebug reads GBufferA RT to SR");
        CheckReadTransition(
            FindPassState(plan, "GBufferDebug", "GBufferDepth"),
            Access::DepthWrite,
            Access::ShaderResource,
            "GBufferDebug reads depth DepthWrite to SR");
        CheckWriteTransition(
            FindPassState(plan, "GBufferDebug", "BackBuffer"),
            Access::Present,
            Access::RenderTarget,
            "GBufferDebug writes BackBuffer");
    }

    {
        GraphBuilder builder;
        BuildRasterFrameGraph(builder, 1280, 720, RasterPresent::Final);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Allocate();
        Check(executor.GetErrors().empty(), "Final Allocate without SetDevice succeeds");
        Check(executor.GetAllocationStats().textureCount == 5, "Final Allocate mints five textures");
        Check(executor.GetAllocationStats().estimatedBytes == 1280ull * 720ull * 28ull,
            "Final Allocate bytes are four GBuffers plus HDR");
    }

    {
        GraphBuilder builder;
        BuildRasterFrameGraph(builder, 1280, 720, RasterPresent::GBufferDebug);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Allocate();
        Check(executor.GetErrors().empty(), "GBufferDebug Allocate without SetDevice succeeds");
        Check(executor.GetAllocationStats().textureCount == 4, "GBufferDebug Allocate mints four textures");
        Check(executor.GetAllocationStats().estimatedBytes == 1280ull * 720ull * 20ull,
            "GBufferDebug Allocate bytes omit HDR");
    }

    {
        GraphBuilder graph;
        TextureHandle a = graph.CreateTexture({"A", 4, 4, Format::RGBA8Unorm});
        TextureHandle b = graph.CreateTexture({"B", 4, 4, Format::RGBA8Unorm});
        TextureHandle output = graph.ImportTexture({"BackBuffer", 4, 4, Format::SRGBA8Unorm});
        std::vector<std::string> ran;
        PassBuilder first = graph.AddPass("First", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext&) {
            ran.push_back("First");
        });
        a = first.Write(a);
        PassBuilder second = graph.AddPass("Second", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext&) {
            ran.push_back("Second");
        });
        second.Read(a);
        b = second.Write(b);
        PassBuilder third = graph.AddPass("Third", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext&) {
            ran.push_back("Third");
        });
        third.Read(b);
        output = third.Write(output);
        graph.ExportTexture(output);

        const CompileResult compiled = GraphCompiler::Compile(graph);
        GraphExecutor executor(graph, compiled);
        executor.Execute(nullptr);
        Check(executor.GetErrors().empty(), "Execute of a fully bound graph has no errors");
        Check(ran.size() == compiled.GetLivePassOrder().size(), "Execute visits every live pass");
        bool orderMatches = ran.size() == 3 && ran[0] == "First" && ran[1] == "Second" && ran[2] == "Third";
        if (orderMatches)
        {
            for (size_t index = 0; index < ran.size(); ++index)
            {
                const uint32_t passIndex = compiled.GetLivePassOrder()[index];
                if (graph.GetPass(passIndex).name != ran[index])
                {
                    orderMatches = false;
                }
            }
        }
        Check(orderMatches, "Execute lambda order equals GetLivePassOrder");
    }

    {
        GraphBuilder graph;
        TextureHandle output = graph.ImportTexture({"BackBuffer", 4, 4, Format::SRGBA8Unorm});
        bool culledRan = false;
        bool liveRan = false;
        graph.AddPass("Culled", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext&) { culledRan = true; });
        PassBuilder live = graph.AddPass("Live", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext&) {
            liveRan = true;
        });
        output = live.Write(output);
        graph.ExportTexture(output);
        GraphExecutor executor(graph, GraphCompiler::Compile(graph));
        executor.Execute(nullptr);
        Check(liveRan && !culledRan, "Execute skips a culled pass lambda");
        Check(!HasCategory(executor.GetErrors(), ErrorCategory::InvalidPass),
            "skipping a culled pass is not InvalidPass");
    }

    {
        GraphBuilder graph;
        TextureHandle internal = graph.CreateTexture({"Temp", 4, 4, Format::RGBA8Unorm});
        TextureHandle output = graph.ImportTexture({"BackBuffer", 4, 4, Format::SRGBA8Unorm});
        bool secondRan = false;
        PassBuilder first = graph.AddPass("First", PassFlags::Raster, PassLambda{});
        internal = first.Write(internal);
        PassBuilder second = graph.AddPass("Second", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext&) {
            secondRan = true;
        });
        second.Read(internal);
        output = second.Write(output);
        graph.ExportTexture(output);
        GraphExecutor executor(graph, GraphCompiler::Compile(graph));
        executor.Execute(nullptr);
        Check(!secondRan, "missing lambda stops later passes");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::InvalidPass),
            "live pass without a lambda is InvalidPass");
    }

    {
        GraphBuilder builder;
        const RasterFrameGraph graph =
            BuildRasterFrameGraph(builder, 8, 8, RasterPresent::Final);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Allocate();
        executor.Execute(nullptr);
        Check(HasCategory(executor.GetErrors(), ErrorCategory::InvalidPass),
            "Execute without a pass lambda is InvalidPass");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnregisteredImport) == false,
            "missing lambdas stop before an unregistered BackBuffer lookup");
        (void)graph;
    }

    {
        GraphBuilder graph;
        RasterFrameGraph built{};
        built.gbufferA = graph.CreateTexture({"GBufferA", 8, 8, Format::SRGBA8Unorm});
        built.gbufferB = graph.CreateTexture({"GBufferB", 8, 8, Format::RGBA16Float});
        built.gbufferC = graph.CreateTexture({"GBufferC", 8, 8, Format::RGBA8Unorm});
        built.gbufferDepth = graph.CreateTexture({"GBufferDepth", 8, 8, Format::D32Float});
        built.hdrCreated = graph.CreateTexture({"HDRSceneColor", 8, 8, Format::RGBA16Float});
        built.outputImported = graph.ImportTexture({"BackBuffer", 8, 8, Format::SRGBA8Unorm});
        const PhysicalTexture* backBuffer = nullptr;
        PassBuilder gbuffer = graph.AddPass("GBuffer", PassFlags::Raster, [](nvrhi::ICommandList*, PassContext&) {});
        built.gbufferA = gbuffer.Write(built.gbufferA);
        built.gbufferB = gbuffer.Write(built.gbufferB);
        built.gbufferC = gbuffer.Write(built.gbufferC);
        built.gbufferDepth = gbuffer.Write(built.gbufferDepth);
        PassBuilder lighting =
            graph.AddPass("DeferredLighting", PassFlags::Raster, [](nvrhi::ICommandList*, PassContext&) {});
        lighting.Read(built.gbufferA);
        lighting.Read(built.gbufferB);
        lighting.Read(built.gbufferC);
        lighting.Read(built.gbufferDepth);
        built.hdrWritten = lighting.Write(built.hdrCreated);
        PassBuilder post = graph.AddPass(
            "PostProcess", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
                backBuffer = ctx.GetTexture(built.outputWritten);
            });
        post.Read(built.hdrWritten);
        built.outputWritten = post.Write(built.outputImported);
        graph.ExportTexture(built.outputWritten);
        GraphExecutor executor(graph, GraphCompiler::Compile(graph));
        executor.Allocate();
        executor.Execute(nullptr);
        Check(backBuffer == nullptr, "unregistered BackBuffer resolves to null");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnregisteredImport),
            "UnregisteredImport of BackBuffer");
    }

    return g_failures;
}
