#include "rdg/GraphCompiler.h"
#include "rdg/M1ShapedGraph.h"
#include "rdg/exec/GraphExecutor.h"

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
        GraphBuilder builder;
        TextureHandle backBuffer = builder.ImportTexture({"BackBuffer", 1280, 720, Format::SRGBA8Unorm});
        PassBuilder pass = builder.AddPass("Present", PassFlags::Raster);
        backBuffer = pass.Write(backBuffer);
        builder.ExportTexture(backBuffer);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.RegisterImport(backBuffer, PhysicalTexture{&dummy, "BackBuffer"});
        const PhysicalTexture* resolved = nullptr;
        executor.ExecutePass(0, [&](PassContext& ctx) { resolved = ctx.GetTexture(backBuffer); });
        Check(resolved != nullptr && resolved->native == &dummy,
              "Declared write of imported back buffer resolves during ExecutePass");
        Check(executor.GetErrors().empty(), "Declared resolve records no errors");
    }

    {
        int dummy = 9;
        GraphBuilder builder;
        TextureHandle imported = builder.ImportTexture({"Input", 8, 8, Format::RGBA8Unorm});
        PassBuilder pass = builder.AddPass("Sample", PassFlags::Raster | PassFlags::NeverCull);
        pass.Read(imported);
        GraphExecutor executor(builder, GraphCompiler::Compile(builder));
        executor.RegisterImport(imported, PhysicalTexture{&dummy, "Input"});
        const PhysicalTexture* resolved = nullptr;
        executor.ExecutePass(0, [&](PassContext& ctx) { resolved = ctx.GetTexture(imported); });
        Check(resolved != nullptr && resolved->native == &dummy, "Read-only imported texture resolves at v0");
        Check(executor.GetErrors().empty(), "Read-only resolve records no errors");
    }

    {
        int nativeA = 10;
        int nativeB = 11;
        GraphBuilder builder;
        GraphBuilder other;
        TextureHandle a = builder.ImportTexture({"A", 8, 8, Format::RGBA8Unorm});
        TextureHandle b = builder.ImportTexture({"B", 8, 8, Format::RGBA8Unorm});
        BufferHandle buffer = builder.ImportBuffer({"Buf", 16, 2});
        PassBuilder pass0 = builder.AddPass("PassA", PassFlags::Raster);
        const TextureHandle aV0 = a;
        a = pass0.Write(a);
        builder.ExportTexture(a);
        PassBuilder pass1 = builder.AddPass("PassB", PassFlags::Raster);
        b = pass1.Write(b);
        buffer = pass1.Write(buffer);
        builder.ExportTexture(b);
        builder.ExportBuffer(buffer);
        const CompileResult compiled = GraphCompiler::Compile(builder);
        Check(compiled.IsSuccess(), "Two-import graph compiles");

        GraphExecutor executor(builder, compiled);
        executor.RegisterImport(a, PhysicalTexture{&nativeA, "A"});
        executor.RegisterImport(b, PhysicalTexture{&nativeB, "B"});
        int bufferNative = 12;
        executor.RegisterImport(buffer, PhysicalBuffer{&bufferNative, "Buf"});

        executor.ExecutePass(0, [&](PassContext& ctx) {
            Check(ctx.GetTexture(a) != nullptr && ctx.GetTexture(a)->native == &nativeA,
                  "Pass A resolves declared A");
            Check(ctx.GetTexture(b) == nullptr, "Pass A must not resolve B");
            Check(ctx.GetTexture(aV0) == nullptr, "Pass A GetTexture of superseded A v0 fails");
            TextureHandle future{a.index, a.version + 1, builder.GetGraphId()};
            Check(ctx.GetTexture(future) == nullptr, "Forged future version of A fails");
            Check(ctx.GetTexture({}) == nullptr, "Null GetTexture fails");
            TextureHandle foreign{a.index, a.version, other.GetGraphId()};
            Check(ctx.GetTexture(foreign) == nullptr, "Foreign-graph GetTexture fails");
            TextureHandle asTexture{buffer.index, buffer.version, builder.GetGraphId()};
            Check(ctx.GetTexture(asTexture) == nullptr, "Type-mismatched GetTexture fails");
        });
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

        executor.ExecutePass(1, [&](PassContext& ctx) {
            Check(ctx.GetTexture(b) != nullptr && ctx.GetTexture(b)->native == &nativeB,
                  "Pass B resolves declared B independently");
            Check(ctx.GetTexture(a) == nullptr, "Pass B must not resolve A");
            Check(ctx.GetBuffer(buffer) != nullptr && ctx.GetBuffer(buffer)->native == &bufferNative,
                  "Declared imported buffer resolves during ExecutePass");
        });
        const PhysicalBuffer* exportedBuffer = executor.GetExported(buffer);
        Check(exportedBuffer != nullptr && exportedBuffer->native == &bufferNative,
              "GetExported returns the registered buffer native");

        PassContext* held = nullptr;
        executor.ExecutePass(0, [&](PassContext& ctx) { held = &ctx; });
        Check(held != nullptr && held->GetTexture(a) == nullptr, "Lookup after the pass has ended fails");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::ExpiredContext),
              "Post-pass GetTexture is ExpiredContext");
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
        int dummy = 13;
        executor.RegisterImport(output, PhysicalTexture{&dummy, "Out"});
        executor.ExecutePass(1, [&](PassContext& ctx) {
            Check(ctx.GetTexture(internal) == nullptr, "Declared internal CreateTexture has no physical in S5.1");
        });
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnregisteredImport),
              "GetTexture of an unbound internal is UnregisteredImport");
    }

    {
        GraphBuilder builder;
        TextureHandle output = builder.ImportTexture({"Out", 8, 8, Format::RGBA8Unorm});
        builder.AddPass("Culled", PassFlags::Raster);
        PassBuilder live = builder.AddPass("Live", PassFlags::Raster);
        output = live.Write(output);
        builder.ExportTexture(output);
        const CompileResult compiled = GraphCompiler::Compile(builder);
        Check(compiled.IsSuccess(), "Graph with a culled empty pass compiles");
        Check(compiled.GetPassCullStates()[0].culled, "Empty pass is culled");

        GraphExecutor executor(builder, compiled);
        int dummy = 14;
        executor.RegisterImport(output, PhysicalTexture{&dummy, "Out"});
        bool culledCalled = false;
        executor.ExecutePass(0, [&](PassContext&) { culledCalled = true; });
        Check(!culledCalled, "ExecutePass does not invoke the callback for a culled pass");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::InvalidPass),
              "ExecutePass on a culled pass is InvalidPass");

        bool oobCalled = false;
        executor.ExecutePass(99, [&](PassContext&) { oobCalled = true; });
        Check(!oobCalled, "ExecutePass does not invoke the callback for an out-of-range pass");
        Check(HasCategory(executor.GetErrors(), ErrorCategory::InvalidPass),
              "ExecutePass(99) is InvalidPass");
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
        GraphExecutor executor(builder, compiled);
        executor.RegisterImport(backBuffer, PhysicalTexture{&native, "BackBuffer"});

        executor.ExecutePass(2, [&](PassContext& ctx) {
            Check(ctx.GetTexture(backBuffer) != nullptr && ctx.GetTexture(backBuffer)->native == &native,
                  "Only PostProcess may resolve BackBuffer");
        });
        executor.ExecutePass(0, [&](PassContext& ctx) {
            Check(ctx.GetTexture(backBuffer) == nullptr, "GBuffer must not resolve BackBuffer");
        });
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UndeclaredAccess),
              "GBuffer BackBuffer lookup is UndeclaredAccess");

        executor.ExecutePass(1, [&](PassContext& ctx) {
            Check(ctx.GetTexture(hdr) == nullptr, "Deferred HDR has no physical in S5.1");
            Check(ctx.GetTexture(backBuffer) == nullptr, "Deferred must not resolve BackBuffer");
        });
        Check(HasCategory(executor.GetErrors(), ErrorCategory::UnregisteredImport),
              "Deferred GetTexture(HDRSceneColor) is UnregisteredImport");
    }

    return g_failures;
}
