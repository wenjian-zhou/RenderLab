#include "rdg/GraphCompiler.h"
#include "rdg/M1ShapedGraph.h"
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

    TextureHandle FindTexture(const GraphBuilder& builder, const char* name)
    {
        for (uint32_t i = 0; i < builder.GetResourceCount(); ++i)
        {
            const ResourceRecord& record = builder.GetResource(i);
            if (record.name == name && record.kind == ResourceKind::Texture)
            {
                return TextureHandle{i, record.currentVersion, builder.GetGraphId()};
            }
        }
        return {};
    }
}

int RunRdgExecTests()
{
    std::printf("RenderLab S5.1 RDG exec tests\n");

    {
        int dummy = 7;
        GraphBuilder builder;
        TextureHandle backBuffer = builder.ImportTexture({"BackBuffer", 1280, 720, Format::SRGBA8Unorm});
        PassBuilder pass = builder.AddPass("Present", PassFlags::Raster);
        backBuffer = pass.Write(backBuffer);
        builder.ExportTexture(backBuffer);
        const CompileResult compiled = GraphCompiler::Compile(builder);
        Check(compiled.IsSuccess(), "Imported back buffer graph compiles");

        GraphExecutor executor(builder, compiled);
        executor.RegisterImport(backBuffer, PhysicalTexture{&dummy, "BackBuffer"});
        const PhysicalTexture* exported = executor.GetExported(backBuffer);
        Check(exported != nullptr && exported->native == &dummy, "GetExported returns the registered native");
        Check(exported != nullptr && exported->debugName == "BackBuffer", "GetExported keeps the debug name");
        Check(executor.GetErrors().empty(), "Successful bind records no errors");
    }

    {
        GraphBuilder builder;
        TextureHandle output = builder.ImportTexture({"Out", 8, 8, Format::RGBA8Unorm});
        TextureHandle created = builder.CreateTexture({"Internal", 8, 8, Format::RGBA8Unorm});
        PassBuilder pass = builder.AddPass("P", PassFlags::Raster);
        created = pass.Write(created);
        output = pass.Write(output);
        builder.ExportTexture(output);
        const CompileResult compiled = GraphCompiler::Compile(builder);
        Check(compiled.IsSuccess(), "Internal-plus-imported graph compiles");

        int dummy = 1;
        GraphExecutor executor(builder, compiled);
        executor.RegisterImport(created, PhysicalTexture{&dummy, "Internal"});
        Check(HasCategory(executor.GetErrors(), ErrorCategory::IncompatibleAccess),
              "RegisterImport of a CreateTexture handle is IncompatibleAccess");
        Check(executor.GetExported(output) == nullptr, "Unbound export lookup returns null");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnregisteredImport),
              "GetExported before RegisterImport is UnregisteredImport");
    }

    {
        int first = 1;
        int second = 2;
        GraphBuilder builder;
        TextureHandle imported = builder.ImportTexture({"Dup", 8, 8, Format::RGBA8Unorm});
        PassBuilder pass = builder.AddPass("P", PassFlags::Raster);
        imported = pass.Write(imported);
        builder.ExportTexture(imported);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.RegisterImport(imported, PhysicalTexture{&first, "first"});
        executor.RegisterImport(imported, PhysicalTexture{&second, "second"});
        Check(HasCategory(executor.GetErrors(), ErrorCategory::IncompatibleAccess),
              "Duplicate RegisterImport is IncompatibleAccess");
        const PhysicalTexture* exported = executor.GetExported(imported);
        Check(exported != nullptr && exported->native == &first, "Duplicate bind keeps the first native");
    }

    {
        GraphBuilder builder;
        TextureHandle imported = builder.ImportTexture({"N", 8, 8, Format::RGBA8Unorm});
        PassBuilder pass = builder.AddPass("P", PassFlags::Raster);
        imported = pass.Write(imported);
        builder.ExportTexture(imported);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.RegisterImport(TextureHandle{}, PhysicalTexture{nullptr, "null"});
        Check(HasCategory(executor.GetErrors(), ErrorCategory::NullHandle), "Null RegisterImport is NullHandle");
    }

    {
        GraphBuilder owner;
        TextureHandle imported = owner.ImportTexture({"N", 8, 8, Format::RGBA8Unorm});
        PassBuilder pass = owner.AddPass("P", PassFlags::Raster);
        imported = pass.Write(imported);
        owner.ExportTexture(imported);
        GraphBuilder other;
        other.ImportTexture({"Other", 8, 8, Format::RGBA8Unorm});
        GraphExecutor executor(owner, GraphCompiler::Compile(owner));
        TextureHandle foreign{imported.index, imported.version, other.GetGraphId()};
        int dummy = 3;
        executor.RegisterImport(foreign, PhysicalTexture{&dummy, "foreign"});
        Check(HasCategory(executor.GetErrors(), ErrorCategory::ForeignGraph),
              "Foreign-graph RegisterImport is ForeignGraph");
    }

    {
        GraphBuilder builder;
        TextureHandle texture = builder.ImportTexture({"T", 8, 8, Format::RGBA8Unorm});
        BufferHandle buffer = builder.ImportBuffer({"B", 16, 4});
        PassBuilder pass = builder.AddPass("P", PassFlags::Raster);
        texture = pass.Write(texture);
        buffer = pass.Write(buffer);
        builder.ExportTexture(texture);
        builder.ExportBuffer(buffer);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        int dummy = 4;
        TextureHandle asTexture{buffer.index, buffer.version, builder.GetGraphId()};
        executor.RegisterImport(asTexture, PhysicalTexture{&dummy, "mismatch"});
        Check(HasCategory(executor.GetErrors(), ErrorCategory::TypeMismatch),
              "TextureHandle targeting a buffer is TypeMismatch");
        BufferHandle asBuffer{texture.index, texture.version, builder.GetGraphId()};
        executor.RegisterImport(asBuffer, PhysicalBuffer{&dummy, "mismatch"});
        Check(HasCategory(executor.GetErrors(), ErrorCategory::TypeMismatch),
              "BufferHandle targeting a texture is TypeMismatch");
    }

    {
        GraphBuilder builder;
        TextureHandle imported = builder.ImportTexture({"N", 8, 8, Format::RGBA8Unorm});
        PassBuilder pass = builder.AddPass("P", PassFlags::Raster);
        imported = pass.Write(imported);
        builder.ExportTexture(imported);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        int dummy = 5;
        TextureHandle oob{99, 0, builder.GetGraphId()};
        executor.RegisterImport(oob, PhysicalTexture{&dummy, "oob"});
        Check(HasCategory(executor.GetErrors(), ErrorCategory::StaleVersion),
              "Out-of-range RegisterImport is StaleVersion");
    }

    {
        GraphBuilder builder;
        TextureHandle imported = builder.ImportTexture({"In", 8, 8, Format::RGBA8Unorm});
        PassBuilder pass = builder.AddPass("Sample", PassFlags::Raster | PassFlags::NeverCull);
        pass.Read(imported);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        int dummy = 6;
        executor.RegisterImport(imported, PhysicalTexture{&dummy, "In"});
        Check(executor.GetExported(imported) == nullptr, "Imported-but-not-exported GetExported returns null");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::IncompatibleAccess),
              "GetExported of a non-exported import is IncompatibleAccess");
    }

    {
        GraphBuilder builder;
        TextureHandle importedV0 = builder.ImportTexture({"BB", 8, 8, Format::RGBA8Unorm});
        const TextureHandle v0 = importedV0;
        PassBuilder pass = builder.AddPass("P", PassFlags::Raster);
        TextureHandle importedV1 = pass.Write(importedV0);
        builder.ExportTexture(importedV1);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        int dummy = 8;
        executor.RegisterImport(v0, PhysicalTexture{&dummy, "BB"});
        Check(executor.GetErrors().empty(), "RegisterImport accepts in-range imported v0 after a write");
        const PhysicalTexture* exported = executor.GetExported(importedV1);
        Check(exported != nullptr && exported->native == &dummy, "GetExported uses the pinned exported version");
        Check(executor.GetExported(v0) == nullptr, "GetExported of superseded v0 returns null");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::SupersededUse),
              "GetExported of superseded v0 is SupersededUse");
    }

    {
        int dummy = 7;
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture({"BackBuffer", 1280, 720, Format::SRGBA8Unorm});
        TextureHandle backBuffer;
        const PhysicalTexture* resolved = nullptr;
        PassBuilder pass = graph.AddPass("Present", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            resolved = ctx.GetTexture(backBuffer);
        });
        backBuffer = pass.Write(imported);
        graph.ExportTexture(backBuffer);
        GraphExecutor executor(graph, GraphCompiler::Compile(graph));
        executor.RegisterImport(imported, PhysicalTexture{&dummy, "BackBuffer"});
        executor.Execute(nullptr);
        Check(resolved != nullptr && resolved->native == &dummy,
              "Declared write of imported back buffer resolves during Execute");
        Check(executor.GetErrors().empty(), "Declared resolve records no errors");
    }

    {
        int dummy = 9;
        GraphBuilder graph;
        TextureHandle imported = graph.ImportTexture({"Input", 8, 8, Format::RGBA8Unorm});
        const PhysicalTexture* resolved = nullptr;
        PassBuilder pass = graph.AddPass(
            "Sample", PassFlags::Raster | PassFlags::NeverCull, [&](nvrhi::ICommandList*, PassContext& ctx) {
                resolved = ctx.GetTexture(imported);
            });
        pass.Read(imported);
        GraphExecutor executor(graph, GraphCompiler::Compile(graph));
        executor.RegisterImport(imported, PhysicalTexture{&dummy, "Input"});
        executor.Execute(nullptr);
        Check(resolved != nullptr && resolved->native == &dummy, "Read-only imported texture resolves at v0");
        Check(executor.GetErrors().empty(), "Read-only resolve records no errors");
    }

    {
        int nativeA = 10;
        int nativeB = 11;
        int bufferNative = 12;
        GraphBuilder graph;
        GraphBuilder other;
        TextureHandle aImported = graph.ImportTexture({"A", 8, 8, Format::RGBA8Unorm});
        TextureHandle bImported = graph.ImportTexture({"B", 8, 8, Format::RGBA8Unorm});
        BufferHandle bufferImported = graph.ImportBuffer({"Buf", 16, 2});
        TextureHandle a;
        TextureHandle b;
        BufferHandle buffer;
        const TextureHandle aV0 = aImported;
        PassContext* held = nullptr;
        PassBuilder pass0 = graph.AddPass("PassA", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            held = &ctx;
            Check(ctx.GetTexture(a) != nullptr && ctx.GetTexture(a)->native == &nativeA,
                  "Pass A resolves declared A");
            Check(ctx.GetTexture(b) == nullptr, "Pass A must not resolve B");
            Check(ctx.GetTexture(aV0) == nullptr, "Pass A GetTexture of superseded A v0 fails");
            TextureHandle future{a.index, a.version + 1, graph.GetGraphId()};
            Check(ctx.GetTexture(future) == nullptr, "Forged future version of A fails");
            Check(ctx.GetTexture({}) == nullptr, "Null GetTexture fails");
            TextureHandle foreign{a.index, a.version, other.GetGraphId()};
            Check(ctx.GetTexture(foreign) == nullptr, "Foreign-graph GetTexture fails");
            TextureHandle asTexture{buffer.index, buffer.version, graph.GetGraphId()};
            Check(ctx.GetTexture(asTexture) == nullptr, "Type-mismatched GetTexture fails");
        });
        a = pass0.Write(aImported);
        graph.ExportTexture(a);
        PassBuilder pass1 = graph.AddPass("PassB", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            Check(ctx.GetTexture(b) != nullptr && ctx.GetTexture(b)->native == &nativeB,
                  "Pass B resolves declared B independently");
            Check(ctx.GetTexture(a) == nullptr, "Pass B must not resolve A");
            Check(ctx.GetBuffer(buffer) != nullptr && ctx.GetBuffer(buffer)->native == &bufferNative,
                  "Declared imported buffer resolves during Execute");
        });
        b = pass1.Write(bImported);
        buffer = pass1.Write(bufferImported);
        graph.ExportTexture(b);
        graph.ExportBuffer(buffer);
        const CompileResult compiled = GraphCompiler::Compile(graph);
        Check(compiled.IsSuccess(), "Two-import graph compiles");

        GraphExecutor executor(graph, compiled);
        executor.RegisterImport(aImported, PhysicalTexture{&nativeA, "A"});
        executor.RegisterImport(bImported, PhysicalTexture{&nativeB, "B"});
        executor.RegisterImport(bufferImported, PhysicalBuffer{&bufferNative, "Buf"});
        executor.Execute(nullptr);
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UndeclaredAccess),
              "Undeclared lookup of B on Pass A is UndeclaredAccess");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::SupersededUse),
              "Superseded A v0 lookup is SupersededUse");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::StaleVersion),
              "Future version lookup is StaleVersion");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::NullHandle), "Null GetTexture is NullHandle");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::ForeignGraph),
              "Foreign-graph GetTexture is ForeignGraph");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::TypeMismatch),
              "TextureHandle targeting a buffer is TypeMismatch at GetTexture");
        const PhysicalBuffer* exportedBuffer = executor.GetExported(buffer);
        Check(exportedBuffer != nullptr && exportedBuffer->native == &bufferNative,
              "GetExported returns the registered buffer native");
        Check(held != nullptr && held->GetTexture(a) == nullptr, "Lookup after the pass has ended fails");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::ExpiredContext),
              "Post-pass GetTexture is ExpiredContext");
    }

    {
        GraphBuilder graph;
        TextureHandle outputImported = graph.ImportTexture({"Out", 8, 8, Format::RGBA8Unorm});
        TextureHandle internal = graph.CreateTexture({"Temp", 8, 8, Format::RGBA8Unorm});
        TextureHandle output;
        PassBuilder produce = graph.AddPass("Produce", PassFlags::Raster, [](nvrhi::ICommandList*, PassContext&) {});
        internal = produce.Write(internal);
        PassBuilder consume = graph.AddPass("Consume", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            Check(ctx.GetTexture(internal) == nullptr, "Declared internal CreateTexture has no physical in S5.1");
        });
        consume.Read(internal);
        output = consume.Write(outputImported);
        graph.ExportTexture(output);
        GraphExecutor executor(graph, GraphCompiler::Compile(graph));
        int dummy = 13;
        executor.RegisterImport(outputImported, PhysicalTexture{&dummy, "Out"});
        executor.Execute(nullptr);
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnregisteredImport),
              "GetTexture of an unbound internal is UnregisteredImport");
    }

    {
        GraphBuilder graph;
        TextureHandle outputImported = graph.ImportTexture({"Out", 8, 8, Format::RGBA8Unorm});
        bool culledCalled = false;
        graph.AddPass("Culled", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext&) { culledCalled = true; });
        TextureHandle output;
        PassBuilder live = graph.AddPass("Live", PassFlags::Raster, [](nvrhi::ICommandList*, PassContext&) {});
        output = live.Write(outputImported);
        graph.ExportTexture(output);
        const CompileResult compiled = GraphCompiler::Compile(graph);
        Check(compiled.IsSuccess(), "Graph with a culled empty pass compiles");
        Check(compiled.GetPassCullStates()[0].culled, "Empty pass is culled");

        GraphExecutor executor(graph, compiled);
        int dummy = 14;
        executor.RegisterImport(outputImported, PhysicalTexture{&dummy, "Out"});
        executor.Execute(nullptr);
        Check(!culledCalled, "Execute does not invoke the lambda for a culled pass");
        Check(!HasCategory(executor.GetErrors(), ErrorCategory::InvalidPass),
              "A culled pass is skipped instead of InvalidPass");
    }

    {
        GraphBuilder graph;
        TextureHandle outputImported = graph.ImportTexture({"Out", 8, 8, Format::RGBA8Unorm});
        TextureHandle output;
        bool called = false;
        PassBuilder pass = graph.AddPass("Live", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext&) {
            called = true;
        });
        output = pass.Write(outputImported);
        graph.ExportTexture(output);
        graph.CreateTexture({"", 8, 8, Format::RGBA8Unorm});
        const CompileResult compiled = GraphCompiler::Compile(graph);
        Check(!compiled.IsSuccess(), "Builder declaration errors fail compilation");

        GraphExecutor executor(graph, compiled);
        executor.Execute(nullptr);
        Check(!called, "Execute does not invoke a lambda when compile failed");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::InvalidPass),
              "Execute on a failed compile is InvalidPass");
    }

    {
        GraphBuilder builder;
        BuildM1ShapedGraph(builder);
        const CompileResult compiled = GraphCompiler::Compile(builder);
        Check(compiled.IsSuccess(), "M1-shaped graph compiles");

        const TextureHandle backBuffer = FindTexture(builder, "BackBuffer");
        const TextureHandle hdr = FindTexture(builder, "HDRSceneColor");
        Check(!backBuffer.IsNull() && !hdr.IsNull(), "M1 BackBuffer and HDRSceneColor handles resolve by name");

        int native = 1;
        GraphBuilder graph;
        TextureHandle gbufferA = graph.CreateTexture({"GBufferA", 1280, 720, Format::SRGBA8Unorm});
        TextureHandle gbufferB = graph.CreateTexture({"GBufferB", 1280, 720, Format::RGBA16Float});
        TextureHandle gbufferC = graph.CreateTexture({"GBufferC", 1280, 720, Format::RGBA8Unorm});
        TextureHandle gbufferDepth = graph.CreateTexture({"GBufferDepth", 1280, 720, Format::D32Float});
        TextureHandle hdrCreated = graph.CreateTexture({"HDRSceneColor", 1280, 720, Format::RGBA16Float});
        TextureHandle backImported = graph.ImportTexture({"BackBuffer", 1280, 720, Format::SRGBA8Unorm});
        TextureHandle hdrWritten;
        TextureHandle backWritten;
        PassBuilder gbufferPass = graph.AddPass("GBuffer", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            Check(ctx.GetTexture(backWritten) == nullptr, "GBuffer must not resolve BackBuffer");
        });
        gbufferA = gbufferPass.Write(gbufferA);
        gbufferB = gbufferPass.Write(gbufferB);
        gbufferC = gbufferPass.Write(gbufferC);
        gbufferDepth = gbufferPass.Write(gbufferDepth);
        PassBuilder deferredPass =
            graph.AddPass("DeferredLighting", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
                Check(ctx.GetTexture(hdrWritten) == nullptr, "Deferred HDR has no physical in S5.1");
                Check(ctx.GetTexture(backWritten) == nullptr, "Deferred must not resolve BackBuffer");
            });
        deferredPass.Read(gbufferA);
        deferredPass.Read(gbufferB);
        deferredPass.Read(gbufferC);
        deferredPass.Read(gbufferDepth);
        hdrWritten = deferredPass.Write(hdrCreated);
        PassBuilder postPass = graph.AddPass("PostProcess", PassFlags::Raster, [&](nvrhi::ICommandList*, PassContext& ctx) {
            Check(ctx.GetTexture(backWritten) != nullptr && ctx.GetTexture(backWritten)->native == &native,
                  "Only PostProcess may resolve BackBuffer");
        });
        postPass.Read(hdrWritten);
        backWritten = postPass.Write(backImported);
        graph.ExportTexture(backWritten);

        GraphExecutor executor(graph, GraphCompiler::Compile(graph));
        executor.RegisterImport(backImported, PhysicalTexture{&native, "BackBuffer"});
        executor.Execute(nullptr);
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UndeclaredAccess),
              "GBuffer BackBuffer lookup is UndeclaredAccess");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnregisteredImport),
              "Deferred GetTexture(HDRSceneColor) is UnregisteredImport");
    }

    return g_failures;
}
