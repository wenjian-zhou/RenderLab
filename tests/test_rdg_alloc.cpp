#include "rdg/GraphCompiler.h"
#include "rdg/exec/FormatMap.h"
#include "rdg/exec/GraphExecutor.h"

#include <cstdio>
#include <span>
#include <string>
#include <utility>

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

    struct M1ExecGraph
    {
        ExecGraph graph;
        TextureHandle gbufferA;
        TextureHandle gbufferB;
        TextureHandle gbufferC;
        TextureHandle gbufferDepth;
        TextureHandle hdr;
        TextureHandle backBuffer;
        TextureHandle backImported;
    };

    void BuildM1Exec(
        M1ExecGraph& built, PassLambda gbufferPass, PassLambda deferredPass, PassLambda postPass)
    {
        TextureHandle gbufferA = built.graph.CreateTexture({"GBufferA", 1280, 720, Format::SRGBA8Unorm});
        TextureHandle gbufferB = built.graph.CreateTexture({"GBufferB", 1280, 720, Format::RGBA16Float});
        TextureHandle gbufferC = built.graph.CreateTexture({"GBufferC", 1280, 720, Format::RGBA8Unorm});
        TextureHandle gbufferDepth = built.graph.CreateTexture({"GBufferDepth", 1280, 720, Format::D32Float});
        TextureHandle hdr = built.graph.CreateTexture({"HDRSceneColor", 1280, 720, Format::RGBA16Float});
        built.backImported = built.graph.ImportTexture({"BackBuffer", 1280, 720, Format::SRGBA8Unorm});

        PassBuilder gbuffer = built.graph.AddPass("GBuffer", PassFlags::Raster, std::move(gbufferPass));
        built.gbufferA = gbuffer.Write(gbufferA);
        built.gbufferB = gbuffer.Write(gbufferB);
        built.gbufferC = gbuffer.Write(gbufferC);
        built.gbufferDepth = gbuffer.Write(gbufferDepth);

        PassBuilder deferred =
            built.graph.AddPass("DeferredLighting", PassFlags::Raster, std::move(deferredPass));
        deferred.Read(built.gbufferA);
        deferred.Read(built.gbufferB);
        deferred.Read(built.gbufferC);
        deferred.Read(built.gbufferDepth);
        built.hdr = deferred.Write(hdr);

        PassBuilder post = built.graph.AddPass("PostProcess", PassFlags::Raster, std::move(postPass));
        post.Read(built.hdr);
        built.backBuffer = post.Write(built.backImported);
        built.graph.ExportTexture(built.backBuffer);
    }

    PassLambda Noop()
    {
        return [](nvrhi::ICommandList*, PassContext&) {};
    }

    void RunFormatMapTests()
    {
        Check(ToNvFormat(Format::Unknown) == nvrhi::Format::UNKNOWN, "Unknown maps to UNKNOWN");
        Check(FromNvFormat(nvrhi::Format::UNKNOWN) == Format::Unknown, "UNKNOWN maps back to Unknown");
        Check(BytesPerPixel(Format::Unknown) == 0, "Unknown has 0 bytes per pixel");

        Check(ToNvFormat(Format::SRGBA8Unorm) == nvrhi::Format::SRGBA8_UNORM, "SRGBA8Unorm maps to SRGBA8_UNORM");
        Check(FromNvFormat(nvrhi::Format::SRGBA8_UNORM) == Format::SRGBA8Unorm, "SRGBA8_UNORM maps back");
        Check(BytesPerPixel(Format::SRGBA8Unorm) == 4, "SRGBA8Unorm is 4 bpp");

        Check(ToNvFormat(Format::RGBA8Unorm) == nvrhi::Format::RGBA8_UNORM, "RGBA8Unorm maps to RGBA8_UNORM");
        Check(FromNvFormat(nvrhi::Format::RGBA8_UNORM) == Format::RGBA8Unorm, "RGBA8_UNORM maps back");
        Check(BytesPerPixel(Format::RGBA8Unorm) == 4, "RGBA8Unorm is 4 bpp");

        Check(ToNvFormat(Format::RGBA16Float) == nvrhi::Format::RGBA16_FLOAT, "RGBA16Float maps to RGBA16_FLOAT");
        Check(FromNvFormat(nvrhi::Format::RGBA16_FLOAT) == Format::RGBA16Float, "RGBA16_FLOAT maps back");
        Check(BytesPerPixel(Format::RGBA16Float) == 8, "RGBA16Float is 8 bpp");

        Check(ToNvFormat(Format::RGBA32Float) == nvrhi::Format::RGBA32_FLOAT, "RGBA32Float maps to RGBA32_FLOAT");
        Check(FromNvFormat(nvrhi::Format::RGBA32_FLOAT) == Format::RGBA32Float, "RGBA32_FLOAT maps back");
        Check(BytesPerPixel(Format::RGBA32Float) == 16, "RGBA32Float is 16 bpp");

        Check(ToNvFormat(Format::R32Float) == nvrhi::Format::R32_FLOAT, "R32Float maps to R32_FLOAT");
        Check(FromNvFormat(nvrhi::Format::R32_FLOAT) == Format::R32Float, "R32_FLOAT maps back");
        Check(BytesPerPixel(Format::R32Float) == 4, "R32Float is 4 bpp");

        Check(ToNvFormat(Format::D32Float) == nvrhi::Format::D32, "D32Float maps to D32");
        Check(FromNvFormat(nvrhi::Format::D32) == Format::D32Float, "D32 maps back");
        Check(BytesPerPixel(Format::D32Float) == 4, "D32Float is 4 bpp");

        const nvrhi::TextureDesc color = MakeTextureDesc({"Color", 64, 32, Format::RGBA8Unorm});
        Check(color.width == 64 && color.height == 32, "Color desc copies size");
        Check(color.debugName == "Color", "Color desc copies debug name");
        Check(color.format == nvrhi::Format::RGBA8_UNORM, "Color desc uses RGBA8_UNORM");
        Check(color.dimension == nvrhi::TextureDimension::Texture2D, "Color desc is Texture2D");
        Check(color.mipLevels == 1 && color.sampleCount == 1, "Color desc is 1 mip 1 sample");
        Check(color.isRenderTarget && color.isShaderResource && !color.isUAV && !color.isTypeless,
              "Color desc is RT+SRV, not UAV or typeless");
        Check(color.initialState == nvrhi::ResourceStates::RenderTarget, "Color initial state is RenderTarget");
        Check(color.keepInitialState, "Color keepInitialState is true");
        Check(color.useClearValue && color.clearValue == nvrhi::Color(0.f, 0.f, 0.f, 1.f),
              "UNORM clear is (0,0,0,1)");

        const nvrhi::TextureDesc depth = MakeTextureDesc({"Depth", 16, 16, Format::D32Float});
        Check(depth.format == nvrhi::Format::D32, "Depth desc uses D32");
        Check(depth.isTypeless, "Depth desc is typeless");
        Check(depth.initialState == nvrhi::ResourceStates::DepthWrite, "Depth initial state is DepthWrite");
        Check(depth.keepInitialState, "Depth keepInitialState is true");
        Check(depth.isRenderTarget && depth.isShaderResource && !depth.isUAV,
              "Depth desc is RT+SRV, not UAV");
        Check(depth.useClearValue && depth.clearValue == nvrhi::Color(0.f), "Depth clear is 0");

        const nvrhi::TextureDesc hdr = MakeTextureDesc({"Hdr", 8, 8, Format::RGBA16Float});
        Check(hdr.format == nvrhi::Format::RGBA16_FLOAT, "Hdr desc uses RGBA16_FLOAT");
        Check(hdr.useClearValue && hdr.clearValue == nvrhi::Color(0.f, 0.f, 0.f, 0.f),
              "float color clear is (0,0,0,0)");
        Check(!hdr.isTypeless && hdr.initialState == nvrhi::ResourceStates::RenderTarget,
              "Hdr desc is color path");

        const nvrhi::TextureDesc r32 = MakeTextureDesc({"R32", 8, 8, Format::R32Float});
        Check(r32.format == nvrhi::Format::R32_FLOAT, "R32 desc uses R32_FLOAT");
        Check(!r32.isTypeless && r32.initialState == nvrhi::ResourceStates::RenderTarget,
              "R32Float is color path, not depth");

        const nvrhi::BufferDesc buffer = MakeBufferDesc({"Buf", 16, 4});
        Check(buffer.byteSize == 64, "Buffer byteSize is bytesPerElement * numElements");
        Check(buffer.structStride == 16, "Buffer structStride is bytesPerElement");
        Check(buffer.debugName == "Buf", "Buffer desc copies debug name");
        Check(buffer.canHaveRawViews, "Buffer canHaveRawViews is true");
        Check(buffer.initialState == nvrhi::ResourceStates::ShaderResource, "Buffer initial state is ShaderResource");
        Check(buffer.keepInitialState, "Buffer keepInitialState is true");
    }
}

int RunRdgAllocTests()
{
    std::printf("RenderLab S5.2 RDG alloc tests\n");
    RunFormatMapTests();

    {
        ExecGraph graph;
        TextureHandle outputImported = graph.ImportTexture({"Out", 8, 8, Format::RGBA8Unorm});
        TextureHandle internalCreated = graph.CreateTexture({"Temp", 8, 8, Format::RGBA8Unorm});
        TextureHandle internal;
        TextureHandle output;
        const PhysicalTexture* resolved = nullptr;
        PassBuilder produce = graph.AddPass("Produce", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            resolved = ctx.GetTexture(internal);
        });
        internal = produce.Write(internalCreated);
        PassBuilder consume = graph.AddPass("Consume", PassFlags::Raster, Noop());
        consume.Read(internal);
        output = consume.Write(outputImported);
        graph.ExportTexture(output);
        GraphExecutor executor(graph, GraphCompiler::Compile(graph.Builder()));
        const AllocationStats before = executor.GetAllocationStats();
        Check(before.textureCount == 0 && before.bufferCount == 0 && before.estimatedBytes == 0,
              "Stats are zero before Allocate");
        executor.Allocate();
        executor.Execute(nullptr);
        Check(resolved != nullptr && resolved->native != nullptr, "Allocated internal texture resolves");
        Check(resolved != nullptr && resolved->debugName == "Temp", "Allocated texture keeps debug name");
        Check(executor.GetErrors().empty(), "Successful Allocate records no errors");
    }

    {
        ExecGraph graph;
        TextureHandle outputImported = graph.ImportTexture({"Out", 8, 8, Format::RGBA8Unorm});
        BufferHandle internalCreated = graph.CreateBuffer({"Scratch", 16, 4});
        BufferHandle internal;
        TextureHandle output;
        const PhysicalBuffer* resolved = nullptr;
        PassBuilder produce = graph.AddPass("Produce", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            resolved = ctx.GetBuffer(internal);
        });
        internal = produce.Write(internalCreated);
        PassBuilder consume = graph.AddPass("Consume", PassFlags::Raster, Noop());
        consume.Read(internal);
        output = consume.Write(outputImported);
        graph.ExportTexture(output);
        GraphExecutor executor(graph, GraphCompiler::Compile(graph.Builder()));
        executor.Allocate();
        executor.Execute(nullptr);
        Check(resolved != nullptr && resolved->native != nullptr, "Allocated internal buffer resolves");
        Check(resolved != nullptr && resolved->debugName == "Scratch", "Allocated buffer keeps debug name");
        Check(executor.GetErrors().empty(), "Successful buffer Allocate records no errors");
    }

    {
        int phase = 0;
        const PhysicalTexture* imported = nullptr;
        const PhysicalTexture* allocated = nullptr;
        ExecGraph graph;
        TextureHandle backImported = graph.ImportTexture({"BackBuffer", 8, 8, Format::SRGBA8Unorm});
        TextureHandle internalCreated = graph.CreateTexture({"Temp", 8, 8, Format::RGBA8Unorm});
        TextureHandle internal;
        TextureHandle backBuffer;
        PassBuilder produce = graph.AddPass("Produce", PassFlags::Raster, Noop());
        internal = produce.Write(internalCreated);
        PassBuilder present = graph.AddPass("Present", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            if (phase == 0)
            {
                Check(ctx.GetTexture(backBuffer) == nullptr, "Allocate does not fill imported BackBuffer");
                return;
            }
            imported = ctx.GetTexture(backBuffer);
            allocated = ctx.GetTexture(internal);
        });
        present.Read(internal);
        backBuffer = present.Write(backImported);
        graph.ExportTexture(backBuffer);
        GraphExecutor executor(graph, GraphCompiler::Compile(graph.Builder()));
        executor.Allocate();
        executor.Execute(nullptr);
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnregisteredImport),
              "Imported BackBuffer after Allocate is UnregisteredImport");

        int dummy = 21;
        executor.RegisterImport(backImported, PhysicalTexture{&dummy, "BackBuffer"});
        phase = 1;
        executor.Execute(nullptr);
        Check(imported != nullptr && imported->native == &dummy,
              "RegisterImport still binds BackBuffer after Allocate");
        Check(allocated != nullptr && allocated->native != nullptr && allocated->native != &dummy,
              "Internal native is not the imported dummy");
    }

    {
        GraphBuilder builder;
        TextureHandle output = builder.ImportTexture({"Out", 8, 8, Format::RGBA8Unorm});
        PassBuilder pass = builder.AddPass("Live", PassFlags::Raster);
        output = pass.Write(output);
        builder.ExportTexture(output);
        builder.CreateTexture({"", 8, 8, Format::RGBA8Unorm});
        const CompileResult compiled = GraphCompiler::Compile(builder);
        Check(!compiled.IsSuccess(), "Empty-name CreateTexture fails compile");
        GraphExecutor executor(builder, compiled);
        executor.Allocate();
        Check(HasCategory(executor.GetErrors(), ErrorCategory::InvalidPass),
              "Allocate on failed compile is InvalidPass");
        Check(executor.GetAllocationStats().textureCount == 0, "Failed compile Allocate leaves stats at zero");
    }

    {
        GraphBuilder builder;
        TextureHandle output = builder.ImportTexture({"Out", 8, 8, Format::RGBA8Unorm});
        TextureHandle internal = builder.CreateTexture({"Temp", 8, 8, Format::RGBA8Unorm});
        PassBuilder produce = builder.AddPass("Produce", PassFlags::Raster);
        internal = produce.Write(internal);
        PassBuilder consume = builder.AddPass("Consume", PassFlags::Raster);
        consume.Read(internal);
        output = consume.Write(output);
        builder.ExportTexture(output);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Allocate();
        const AllocationStats afterFirst = executor.GetAllocationStats();
        executor.Allocate();
        Check(HasCategory(executor.GetErrors(), ErrorCategory::IncompatibleAccess),
              "Second Allocate is IncompatibleAccess");
        const AllocationStats afterSecond = executor.GetAllocationStats();
        Check(afterSecond.textureCount == afterFirst.textureCount &&
                  afterSecond.bufferCount == afterFirst.bufferCount &&
                  afterSecond.estimatedBytes == afterFirst.estimatedBytes,
              "Second Allocate does not change stats");
    }

    {
        int dummy = 22;
        const PhysicalTexture* a = nullptr;
        const PhysicalTexture* b = nullptr;
        const PhysicalTexture* c = nullptr;
        const PhysicalTexture* depth = nullptr;
        const PhysicalTexture* hdrPhysical = nullptr;
        const PhysicalTexture* presentBack = nullptr;
        const PhysicalTexture* presentHdr = nullptr;
        M1ExecGraph built;
        BuildM1Exec(
            built,
            [&](nvrhi::ICommandList*, PassContext& ctx) {
                a = ctx.GetTexture(built.gbufferA);
                b = ctx.GetTexture(built.gbufferB);
                c = ctx.GetTexture(built.gbufferC);
                depth = ctx.GetTexture(built.gbufferDepth);
            },
            [&](nvrhi::ICommandList*, PassContext& ctx) { hdrPhysical = ctx.GetTexture(built.hdr); },
            [&](nvrhi::ICommandList*, PassContext& ctx) {
                presentBack = ctx.GetTexture(built.backBuffer);
                presentHdr = ctx.GetTexture(built.hdr);
            });
        const CompileResult compiled = GraphCompiler::Compile(built.graph.Builder());
        Check(compiled.IsSuccess(), "M1-shaped graph compiles for Allocate");
        GraphExecutor executor(built.graph, compiled);
        executor.Allocate();
        executor.RegisterImport(built.backImported, PhysicalTexture{&dummy, "BackBuffer"});
        const AllocationStats stats = executor.GetAllocationStats();
        Check(stats.textureCount == 5, "M1 Allocate mints 5 internal textures");
        Check(stats.bufferCount == 0, "M1 Allocate mints 0 buffers");
        Check(stats.estimatedBytes == 25804800ull, "M1 Allocate estimated bytes are 1280x720 internals");
        executor.Execute(nullptr);
        Check(a != nullptr && a->native != nullptr && a->debugName == "GBufferA", "GBufferA resolves after Allocate");
        Check(b != nullptr && b->native != nullptr && b->debugName == "GBufferB", "GBufferB resolves after Allocate");
        Check(c != nullptr && c->native != nullptr && c->debugName == "GBufferC", "GBufferC resolves after Allocate");
        Check(depth != nullptr && depth->native != nullptr && depth->debugName == "GBufferDepth",
              "GBufferDepth resolves after Allocate");
        Check(a != nullptr && b != nullptr && c != nullptr && depth != nullptr &&
                  a->native != b->native && a->native != c->native && a->native != depth->native &&
                  b->native != c->native && b->native != depth->native && c->native != depth->native,
              "M1 GBuffer natives are distinct");
        Check(hdrPhysical != nullptr && hdrPhysical->native != nullptr && hdrPhysical->debugName == "HDRSceneColor",
              "HDRSceneColor resolves in Deferred after Allocate");
        Check(presentBack != nullptr && presentBack->native == &dummy,
              "PostProcess BackBuffer is the registered dummy");
        Check(presentHdr != nullptr && hdrPhysical != nullptr && presentHdr->native == hdrPhysical->native,
              "PostProcess HDR is the allocated identity");
    }

    {
        void* natives[3][5] = {};
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            M1ExecGraph built;
            BuildM1Exec(
                built,
                [&](nvrhi::ICommandList*, PassContext& ctx) {
                    const PhysicalTexture* textureA = ctx.GetTexture(built.gbufferA);
                    const PhysicalTexture* textureB = ctx.GetTexture(built.gbufferB);
                    const PhysicalTexture* textureC = ctx.GetTexture(built.gbufferC);
                    const PhysicalTexture* textureDepth = ctx.GetTexture(built.gbufferDepth);
                    natives[iteration][0] = textureA ? textureA->native : nullptr;
                    natives[iteration][1] = textureB ? textureB->native : nullptr;
                    natives[iteration][2] = textureC ? textureC->native : nullptr;
                    natives[iteration][3] = textureDepth ? textureDepth->native : nullptr;
                },
                [&](nvrhi::ICommandList*, PassContext& ctx) {
                    const PhysicalTexture* hdr = ctx.GetTexture(built.hdr);
                    natives[iteration][4] = hdr ? hdr->native : nullptr;
                },
                Noop());
            GraphExecutor executor(built.graph, GraphCompiler::Compile(built.graph.Builder()));
            executor.Allocate();
            executor.Execute(nullptr);
            Check(natives[iteration][0] && natives[iteration][1] && natives[iteration][2] && natives[iteration][3] &&
                      natives[iteration][4],
                  "Repeated graph Allocate mints M1 internals");
            Check(natives[iteration][0] != natives[iteration][1] && natives[iteration][0] != natives[iteration][2] &&
                      natives[iteration][0] != natives[iteration][3] && natives[iteration][0] != natives[iteration][4],
                  "Repeated graph internals have distinct natives");
            Check(executor.GetAllocationStats().textureCount == 5, "Repeated graph stats stay 5 textures");
        }
    }

    {
        GraphBuilder builder;
        TextureHandle output = builder.ImportTexture({"Out", 8, 8, Format::RGBA8Unorm});
        TextureHandle internal = builder.CreateTexture({"Temp", 8, 8, Format::RGBA8Unorm});
        PassBuilder produce = builder.AddPass("Produce", PassFlags::Raster);
        internal = produce.Write(internal);
        PassBuilder consume = builder.AddPass("Consume", PassFlags::Raster);
        consume.Read(internal);
        output = consume.Write(output);
        builder.ExportTexture(output);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.Allocate();
        executor.SetDevice(nullptr);
        Check(HasCategory(executor.GetErrors(), ErrorCategory::IncompatibleAccess),
              "SetDevice after Allocate is IncompatibleAccess");
    }

    return g_failures;
}
