#include "rdg/GraphCompiler.h"
#include "rdg/Pass.h"
#include "rdg/ToneMapGraph.h"
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

    struct ToneExec
    {
        GraphBuilder graph;
        TextureHandle hdr;
        TextureHandle outputImported;
        TextureHandle outputWritten;
    };

    void BuildToneExec(ToneExec& built, ToneMapOutput output, PassLambda post)
    {
        built.hdr = built.graph.ImportTexture({"HDRSceneColor", 1280, 720, Format::RGBA16Float});
        const bool exportOutput = output == ToneMapOutput::BackBuffer;
        const char* outputName = exportOutput ? "BackBuffer" : "PostProcessColor";
        built.outputImported = built.graph.ImportTexture({outputName, 1280, 720, Format::SRGBA8Unorm});
        PassBuilder postPass = built.graph.AddPass("PostProcess", PassFlags::Raster, std::move(post));
        postPass.Read(built.hdr);
        built.outputWritten = postPass.Write(built.outputImported);
        if (exportOutput)
        {
            built.graph.ExportTexture(built.outputWritten);
        }
    }
}

int RunRdgTonemapTests()
{
    std::printf("RenderLab S5.4 RDG tone-map tests\n");

    {
        GraphBuilder builder;
        const ToneMapGraph graph =
            BuildToneMapGraph(builder, 1280, 720, ToneMapOutput::BackBuffer);
        const CompileResult result = GraphCompiler::Compile(builder);
        Check(builder.GetErrors().empty() && result.IsSuccess(), "tone-map graph compiles");
        Check(builder.GetResourceCount() == 2, "two resources");
        Check(result.GetLivePassOrder().size() == 1, "one live pass");
        Check(result.GetLivePassOrder()[0] == graph.postProcessPassIndex, "live pass is PostProcess");
        Check((builder.GetPass(graph.postProcessPassIndex).flags & PassFlags::Raster) == PassFlags::Raster,
            "PostProcess is Raster");

        const ResourceRecord& hdr = builder.GetResource(graph.hdrSceneColor.index);
        Check(hdr.name == "HDRSceneColor" && hdr.imported && !hdr.exported,
            "HDRSceneColor imported not exported");
        const ResourceRecord& backBuffer = builder.GetResource(graph.outputImported.index);
        Check(backBuffer.name == "BackBuffer" && backBuffer.imported && backBuffer.exported,
            "BackBuffer imported and exported");

        const std::string dump = result.Dump();
        Check(dump.find("order: [0 \"PostProcess\"]") != std::string::npos, "dump order is PostProcess");
        Check(dump.find("0 \"PostProcess\" live root-output") != std::string::npos,
            "PostProcess is live root-output");
        Check(dump.find("read texture 0 \"HDRSceneColor\"") != std::string::npos, "dump reads HDRSceneColor");
        Check(dump.find("write texture 1 \"BackBuffer\"") != std::string::npos, "dump writes BackBuffer");
        Check(dump.find("texture 0 \"HDRSceneColor\" imported\n") != std::string::npos,
            "dump HDR imported-only");
        Check(dump.find("texture 1 \"BackBuffer\" imported exported") != std::string::npos,
            "dump BackBuffer imported exported");
        Check(dump.find("0 \"PostProcess\" Raster") != std::string::npos, "dump flags Raster");
    }

    {
        GraphBuilder builder;
        BuildToneMapGraph(builder, 1280, 720, ToneMapOutput::BackBuffer);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        Check(executor.GetAccessPlan().Dump() == "access-plan: empty\n", "Dump empty before Plan");
        executor.Plan();
        Check(executor.GetErrors().empty(), "Plan of tone-map graph succeeds");
        const AccessPlan& plan = executor.GetAccessPlan();
        const ResourceBoundary* hdr = FindBoundary(plan, "HDRSceneColor");
        Check(hdr && hdr->imported && !hdr->exported && hdr->initial == Access::RenderTarget &&
                  hdr->final == Access::RenderTarget,
            "HDR imported-only RT/RT");
        const ResourceBoundary* bb = FindBoundary(plan, "BackBuffer");
        Check(bb && bb->imported && bb->exported && bb->initial == Access::Present &&
                  bb->final == Access::Present,
            "BackBuffer imported+exported Present");
        CheckReadTransition(
            FindPassState(plan, "PostProcess", "HDRSceneColor"),
            Access::RenderTarget,
            Access::ShaderResource,
            "HDR RT to SR");
        CheckWriteTransition(
            FindPassState(plan, "PostProcess", "BackBuffer"),
            Access::Present,
            Access::RenderTarget,
            "BackBuffer Present to RT");
        const ResourceRestore* hdrR = FindRestore(plan, "HDRSceneColor");
        Check(hdrR && hdrR->from == Access::ShaderResource && hdrR->to == Access::RenderTarget,
            "HDR restore SR to RT");
        const ResourceRestore* bbR = FindRestore(plan, "BackBuffer");
        Check(bbR && bbR->from == Access::RenderTarget && bbR->to == Access::Present,
            "BackBuffer restore RT to Present");
    }

    {
        int hdrNative = 1;
        int bbNative = 2;
        const PhysicalTexture* gotHdr = nullptr;
        const PhysicalTexture* gotOut = nullptr;
        ToneExec built;
        BuildToneExec(built, ToneMapOutput::BackBuffer, [&](nvrhi::ICommandList*, PassContext& ctx) {
            gotHdr = ctx.GetTexture(built.hdr);
            gotOut = ctx.GetTexture(built.outputWritten);
        });
        GraphExecutor executor(built.graph, GraphCompiler::Compile(built.graph));
        executor.RegisterImport(built.hdr, PhysicalTexture{&hdrNative, "HDRSceneColor"});
        executor.RegisterImport(built.outputImported, PhysicalTexture{&bbNative, "BackBuffer"});
        executor.Plan();
        Check(!executor.GetAccessPlan().passes.empty(), "Plan consumed before Execute");
        executor.Execute(nullptr);
        Check(gotHdr && gotHdr->native == &hdrNative, "PostProcess resolves HDR");
        Check(gotOut && gotOut->native == &bbNative, "PostProcess resolves written BackBuffer");
        Check(executor.GetErrors().empty(), "registered resolve has no errors");
    }

    {
        ToneExec built;
        BuildToneExec(built, ToneMapOutput::BackBuffer, [&](nvrhi::ICommandList*, PassContext& ctx) {
            Check(ctx.GetTexture(built.hdr) == nullptr, "unregistered HDR is null");
        });
        GraphExecutor executor(built.graph, GraphCompiler::Compile(built.graph));
        executor.Plan();
        executor.Execute(nullptr);
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnregisteredImport),
            "UnregisteredImport on HDR");
    }

    {
        GraphBuilder other;
        int hdrNative = 3;
        int bbNative = 4;
        ToneExec built;
        BuildToneExec(built, ToneMapOutput::BackBuffer, [&](nvrhi::ICommandList*, PassContext& ctx) {
            Check(ctx.GetTexture({}) == nullptr, "null handle is null");
            TextureHandle foreign{built.hdr.index, built.hdr.version, other.GetGraphId()};
            Check(ctx.GetTexture(foreign) == nullptr, "foreign-graph handle is null");
            Check(ctx.GetTexture(built.outputImported) == nullptr, "imported v0 during write is null");
        });
        GraphExecutor executor(built.graph, GraphCompiler::Compile(built.graph));
        executor.RegisterImport(built.hdr, PhysicalTexture{&hdrNative, "HDRSceneColor"});
        executor.RegisterImport(built.outputImported, PhysicalTexture{&bbNative, "BackBuffer"});
        executor.Plan();
        executor.Execute(nullptr);
        Check(HasCategory(executor.GetErrors(), ErrorCategory::NullHandle), "NullHandle on default handle");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::ForeignGraph),
            "ForeignGraph on foreign handle");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::SupersededUse),
            "SupersededUse on outputImported v0");
    }

    {
        GraphBuilder builder;
        const ToneMapGraph graph =
            BuildToneMapGraph(builder, 1280, 720, ToneMapOutput::DumpTarget);
        const CompileResult result = GraphCompiler::Compile(builder);
        Check(result.IsSuccess() && result.GetLivePassOrder().size() == 1,
            "dump-target graph one live pass");
        const std::string dump = result.Dump();
        Check(dump.find("\"PostProcessColor\"") != std::string::npos, "output named PostProcessColor");
        Check(dump.find("\"PostProcessColor\" imported exported") == std::string::npos,
            "PostProcessColor is not exported");
        GraphExecutor executor(builder, result);
        executor.Plan();
        Check(executor.GetErrors().empty(), "Plan of dump-target graph succeeds");
        const AccessPlan& plan = executor.GetAccessPlan();
        const ResourceBoundary* out = FindBoundary(plan, "PostProcessColor");
        Check(out && out->imported && !out->exported && out->initial == Access::RenderTarget &&
                  out->final == Access::RenderTarget,
            "dump target imported-only RT");
        Check(FindRestore(plan, "PostProcessColor") == nullptr, "dump target needs no restore");
        Check(FindRestore(plan, "HDRSceneColor") != nullptr, "HDR still restores SR to RT");
        Check(graph.outputImported.index == out->resourceIndex, "dump target handle matches plan");
    }

    return g_failures;
}
