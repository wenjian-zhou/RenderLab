#include "rdg/GraphCompiler.h"
#include "rdg/M1ShapedGraph.h"
#include "rdg/Pass.h"
#include "rdg/ResourceDesc.h"
#include "rdg/AccessMap.h"
#include "rdg/FormatMap.h"
#include "rdg/GraphExecutor.h"

#include <cstdint>
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

    void CheckFormatMapAgreement(Format format, const char* message)
    {
        const TextureDesc desc{"t", 8, 8, format};
        const Access initial = InferAccess(AccessMode::Write, ResourceKind::Texture, format);
        Check(ToNvStates(initial) == MakeTextureDesc(desc).initialState, message);
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

    const PassResourceState* FindPassState(const AccessPlan& plan, const char* passName, const char* resourceName)
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

    bool HasPassNamed(const AccessPlan& plan, const char* name)
    {
        for (const PassResourceState& state : plan.passes)
        {
            if (state.passName == name)
            {
                return true;
            }
        }
        return false;
    }

    void CheckWriteSame(const PassResourceState* state, Access access, const char* message)
    {
        Check(state != nullptr && state->mode == AccessMode::Write && state->before == access &&
                  state->required == access && state->after == access,
            message);
    }

    void CheckReadTransition(
        const PassResourceState* state,
        Access before,
        Access after,
        const char* message)
    {
        Check(state != nullptr && state->mode == AccessMode::Read && state->before == before &&
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

int RunRdgAccessTests()
{
    std::printf("RenderLab S5.3 RDG access tests\n");

    Check(static_cast<uint32_t>(Access::Unknown) == 0, "Unknown is 0");
    Check((Access::ShaderResource | Access::RenderTarget) != Access::ShaderResource, "Access is a flag enum");
    Check((kKnownAccessBits & Access::Present) == Access::Present, "Present is in kKnownAccessBits");
    Check((kWritableMask & kReadableMask) == Access::Unknown, "read/write masks do not overlap");

    Check(ToNvStates(Access::ShaderResource) == nvrhi::ResourceStates::ShaderResource, "SR maps");
    Check(ToNvStates(Access::RenderTarget) == nvrhi::ResourceStates::RenderTarget, "RT maps");
    Check(ToNvStates(Access::DepthWrite) == nvrhi::ResourceStates::DepthWrite, "DepthWrite maps");
    Check(ToNvStates(Access::Present) == nvrhi::ResourceStates::Present, "Present maps");
    Check(FromNvStates(nvrhi::ResourceStates::Present) == Access::Present, "Present round-trips");
    Check(!IsSingleKnownAccess(Access::Unknown), "Unknown rejected");
    Check(IsSingleKnownAccess(Access::ShaderResource), "SR is single known");
    Check(IsSingleKnownAccess(Access::RenderTarget), "RT is single known");
    Check(IsSingleKnownAccess(Access::DepthWrite), "DepthWrite is single known");
    Check(IsSingleKnownAccess(Access::Present), "Present is single known");
    Check(!IsSingleKnownAccess(Access::ShaderResource | Access::RenderTarget), "SR|RT rejected");
    Check(ToNvStates(Access::Unknown) == nvrhi::ResourceStates::Unknown, "Unknown maps to Unknown");
    Check(InferAccess(AccessMode::Read, ResourceKind::Texture, Format::RGBA16Float) == Access::ShaderResource,
        "read is SR");
    Check(InferAccess(AccessMode::Write, ResourceKind::Texture, Format::D32Float) == Access::DepthWrite,
        "depth write");
    Check(InferAccess(AccessMode::Write, ResourceKind::Texture, Format::SRGBA8Unorm) == Access::RenderTarget,
        "color write");
    Check(InferAccess(AccessMode::Write, ResourceKind::Buffer, Format::Unknown) == Access::Unknown,
        "buffer write unmapped");

    CheckFormatMapAgreement(Format::SRGBA8Unorm, "SRGBA8Unorm initial matches FormatMap");
    CheckFormatMapAgreement(Format::RGBA8Unorm, "RGBA8Unorm initial matches FormatMap");
    CheckFormatMapAgreement(Format::RGBA16Float, "RGBA16Float initial matches FormatMap");
    CheckFormatMapAgreement(Format::RGBA32Float, "RGBA32Float initial matches FormatMap");
    CheckFormatMapAgreement(Format::R32Float, "R32Float initial matches FormatMap");
    CheckFormatMapAgreement(Format::D32Float, "D32Float initial matches FormatMap");

    {
        GraphBuilder builder;
        BuildM1ShapedGraph(builder);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        Check(executor.GetAccessPlan().Dump() == "access-plan: empty\n", "Dump empty before Plan");
        executor.Plan();
        Check(executor.GetErrors().empty(), "Plan without Allocate succeeds");
        const AccessPlan& plan = executor.GetAccessPlan();

        const ResourceBoundary* backBuffer = FindBoundary(plan, "BackBuffer");
        Check(backBuffer != nullptr && backBuffer->imported && backBuffer->exported &&
                  backBuffer->initial == Access::Present && backBuffer->final == Access::Present,
            "BackBuffer initial/final Present");
        const ResourceBoundary* gbufferA = FindBoundary(plan, "GBufferA");
        Check(gbufferA != nullptr && gbufferA->initial == Access::RenderTarget &&
                  gbufferA->final == Access::RenderTarget,
            "GBufferA initial/final RenderTarget");
        const ResourceBoundary* gbufferB = FindBoundary(plan, "GBufferB");
        Check(gbufferB != nullptr && gbufferB->initial == Access::RenderTarget &&
                  gbufferB->final == Access::RenderTarget,
            "GBufferB initial/final RenderTarget");
        const ResourceBoundary* gbufferC = FindBoundary(plan, "GBufferC");
        Check(gbufferC != nullptr && gbufferC->initial == Access::RenderTarget &&
                  gbufferC->final == Access::RenderTarget,
            "GBufferC initial/final RenderTarget");
        const ResourceBoundary* depth = FindBoundary(plan, "GBufferDepth");
        Check(depth != nullptr && depth->initial == Access::DepthWrite && depth->final == Access::DepthWrite,
            "GBufferDepth initial/final DepthWrite");
        const ResourceBoundary* hdr = FindBoundary(plan, "HDRSceneColor");
        Check(hdr != nullptr && hdr->initial == Access::RenderTarget && hdr->final == Access::RenderTarget,
            "HDRSceneColor initial/final RenderTarget");

        CheckWriteSame(FindPassState(plan, "GBuffer", "GBufferA"), Access::RenderTarget, "GBuffer writes A as RT");
        CheckWriteSame(FindPassState(plan, "GBuffer", "GBufferB"), Access::RenderTarget, "GBuffer writes B as RT");
        CheckWriteSame(FindPassState(plan, "GBuffer", "GBufferC"), Access::RenderTarget, "GBuffer writes C as RT");
        CheckWriteSame(FindPassState(plan, "GBuffer", "GBufferDepth"), Access::DepthWrite, "GBuffer writes Depth");

        CheckReadTransition(
            FindPassState(plan, "DeferredLighting", "GBufferA"),
            Access::RenderTarget,
            Access::ShaderResource,
            "Deferred reads A RT to SR");
        CheckReadTransition(
            FindPassState(plan, "DeferredLighting", "GBufferB"),
            Access::RenderTarget,
            Access::ShaderResource,
            "Deferred reads B RT to SR");
        CheckReadTransition(
            FindPassState(plan, "DeferredLighting", "GBufferC"),
            Access::RenderTarget,
            Access::ShaderResource,
            "Deferred reads C RT to SR");
        CheckReadTransition(
            FindPassState(plan, "DeferredLighting", "GBufferDepth"),
            Access::DepthWrite,
            Access::ShaderResource,
            "Deferred reads Depth DepthWrite to SR");
        CheckWriteSame(
            FindPassState(plan, "DeferredLighting", "HDRSceneColor"),
            Access::RenderTarget,
            "Deferred writes HDR as RT");

        CheckReadTransition(
            FindPassState(plan, "PostProcess", "HDRSceneColor"),
            Access::RenderTarget,
            Access::ShaderResource,
            "PostProcess reads HDR RT to SR");
        const PassResourceState* postBackBuffer = FindPassState(plan, "PostProcess", "BackBuffer");
        Check(postBackBuffer != nullptr && postBackBuffer->mode == AccessMode::Write &&
                  postBackBuffer->before == Access::Present &&
                  postBackBuffer->required == Access::RenderTarget &&
                  postBackBuffer->after == Access::RenderTarget,
            "PostProcess writes BackBuffer Present to RT");

        const ResourceRestore* restoreBackBuffer = FindRestore(plan, "BackBuffer");
        Check(restoreBackBuffer != nullptr && restoreBackBuffer->from == Access::RenderTarget &&
                  restoreBackBuffer->to == Access::Present,
            "BackBuffer restore RT to Present");
        const ResourceRestore* restoreA = FindRestore(plan, "GBufferA");
        Check(restoreA != nullptr && restoreA->from == Access::ShaderResource &&
                  restoreA->to == Access::RenderTarget,
            "GBufferA restore SR to RT");
        const ResourceRestore* restoreB = FindRestore(plan, "GBufferB");
        Check(restoreB != nullptr && restoreB->from == Access::ShaderResource &&
                  restoreB->to == Access::RenderTarget,
            "GBufferB restore SR to RT");
        const ResourceRestore* restoreC = FindRestore(plan, "GBufferC");
        Check(restoreC != nullptr && restoreC->from == Access::ShaderResource &&
                  restoreC->to == Access::RenderTarget,
            "GBufferC restore SR to RT");
        const ResourceRestore* restoreDepth = FindRestore(plan, "GBufferDepth");
        Check(restoreDepth != nullptr && restoreDepth->from == Access::ShaderResource &&
                  restoreDepth->to == Access::DepthWrite,
            "GBufferDepth restore SR to DepthWrite");
        const ResourceRestore* restoreHdr = FindRestore(plan, "HDRSceneColor");
        Check(restoreHdr != nullptr && restoreHdr->from == Access::ShaderResource &&
                  restoreHdr->to == Access::RenderTarget,
            "HDR restore SR to RT");

        const std::string dump = plan.Dump();
        Check(dump.find("resource 0 \"BackBuffer\" imported exported initial=Present final=Present") !=
                  std::string::npos,
            "Dump names BackBuffer Present boundary");
        Check(dump.find("0 \"BackBuffer\" RenderTarget -> Present") != std::string::npos,
            "Dump restores BackBuffer to Present");
        Check(HasPassNamed(plan, "GBuffer") && HasPassNamed(plan, "DeferredLighting") &&
                  HasPassNamed(plan, "PostProcess"),
            "M1 live passes are in the plan");
        Check(!HasPassNamed(plan, "Extra") && !HasPassNamed(plan, "Unused"),
            "M1 plan has no extra pass names");
    }

    {
        GraphBuilder builder;
        TextureHandle color = builder.CreateTexture({"Color", 8, 8, Format::RGBA8Unorm});
        TextureHandle out = builder.CreateTexture({"Out", 8, 8, Format::RGBA8Unorm});
        PassBuilder writer = builder.AddPass("W", PassFlags::Raster);
        color = writer.Write(color);
        PassBuilder firstRead = builder.AddPass("R1", PassFlags::Raster | PassFlags::NeverCull);
        firstRead.Read(color);
        PassBuilder secondRead = builder.AddPass("R2", PassFlags::Raster);
        secondRead.Read(color);
        out = secondRead.Write(out);
        builder.ExportTexture(out);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Plan();
        const AccessPlan& plan = executor.GetAccessPlan();
        CheckReadTransition(
            FindPassState(plan, "R1", "Color"),
            Access::RenderTarget,
            Access::ShaderResource,
            "Repeated read first use RT to SR");
        CheckReadTransition(
            FindPassState(plan, "R2", "Color"),
            Access::ShaderResource,
            Access::ShaderResource,
            "Repeated read keeps ShaderResource");
    }

    {
        GraphBuilder builder;
        TextureHandle color = builder.CreateTexture({"Color", 8, 8, Format::RGBA8Unorm});
        TextureHandle out = builder.CreateTexture({"Out", 8, 8, Format::RGBA8Unorm});
        PassBuilder producer = builder.AddPass("Producer", PassFlags::Raster);
        color = producer.Write(color);
        PassBuilder extra = builder.AddPass("Extra", PassFlags::Raster);
        extra.Read(color);
        PassBuilder consumer = builder.AddPass("Consumer", PassFlags::Raster);
        consumer.Read(color);
        out = consumer.Write(out);
        builder.ExportTexture(out);
        const CompileResult compiled = GraphCompiler::Compile(builder);
        Check(compiled.IsSuccess(), "Culled-extra graph compiles");
        bool extraCulled = false;
        for (const PassCullState& state : compiled.GetPassCullStates())
        {
            if (state.passIndex == extra.PassIndex())
            {
                extraCulled = state.culled;
            }
        }
        Check(extraCulled, "Extra pass is culled");
        GraphExecutor executor(builder, compiled);
        executor.Plan();
        const AccessPlan& plan = executor.GetAccessPlan();
        Check(!HasPassNamed(plan, "Extra"), "Culled Extra is omitted from the plan");
        Check(HasPassNamed(plan, "Producer") && HasPassNamed(plan, "Consumer"),
            "Live Producer and Consumer remain in the plan");
    }

    {
        GraphBuilder builder;
        BufferHandle buffer = builder.CreateBuffer({"Scratch", 4, 8});
        TextureHandle out = builder.CreateTexture({"Out", 8, 8, Format::RGBA8Unorm});
        PassBuilder pass = builder.AddPass("P", PassFlags::Raster);
        buffer = pass.Write(buffer);
        out = pass.Write(out);
        builder.ExportTexture(out);
        const CompileResult compiled = GraphCompiler::Compile(builder);
        Check(compiled.IsSuccess(), "Buffer-write graph compiles");
        GraphExecutor executor(builder, compiled);
        executor.Plan();
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnknownAccess),
            "Buffer write is UnknownAccess");
        Check(executor.GetAccessPlan().Dump() == "access-plan: empty\n", "Failed Plan leaves dump empty");
        bool namedBuffer = false;
        for (const Error& error : executor.GetErrors())
        {
            if (error.category == ErrorCategory::UnknownAccess && error.resourceName == "Scratch")
            {
                namedBuffer = true;
            }
        }
        Check(namedBuffer, "UnknownAccess names the buffer");
    }

    {
        GraphBuilder builder;
        TextureHandle first = builder.CreateTexture({"First", 8, 8, Format::RGBA8Unorm});
        TextureHandle second = builder.CreateTexture({"Second", 8, 8, Format::RGBA8Unorm});
        PassBuilder passA = builder.AddPass("A", PassFlags::Raster);
        PassBuilder passB = builder.AddPass("B", PassFlags::Raster);
        first = passA.Write(first);
        second = passB.Write(second);
        passA.Read(second);
        passB.Read(first);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Plan();
        Check(HasCategory(executor.GetErrors(), ErrorCategory::InvalidPass),
            "Failed compile Plan is InvalidPass");
        Check(executor.GetAccessPlan().Dump() == "access-plan: empty\n", "Failed compile dump stays empty");
    }

    {
        GraphBuilder builder;
        BuildM1ShapedGraph(builder);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Plan();
        const size_t restoreCount = executor.GetAccessPlan().restores.size();
        executor.Plan();
        Check(HasCategory(executor.GetErrors(), ErrorCategory::IncompatibleAccess),
            "Second Plan is IncompatibleAccess");
        Check(executor.GetAccessPlan().restores.size() == restoreCount, "Second Plan leaves first plan intact");
    }

    {
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture({"Out", 8, 8, Format::RGBA8Unorm});
        TextureHandle output;
        bool ran = false;
        PassBuilder pass = graph.AddPass("P", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext&) {
            ran = true;
        });
        output = pass.Write(imported);
        graph.ExportTexture(output);
        GraphExecutor executor(graph, GraphCompiler::Compile(graph));
        executor.Execute(nullptr);
        Check(ran && executor.GetErrors().empty(), "Execute without Plan still runs");
    }

    return g_failures;
}
