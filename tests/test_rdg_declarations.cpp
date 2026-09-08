#include "rdg/GraphBuilder.h"

#include <cstdio>
#include <string>
#include <variant>

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
}

int RunRdgDeclarationTests()
{
    std::printf("RenderLab S4.1 RDG declaration tests\n");

    // Texture descriptor validation
    {
        GraphBuilder builder;
        TextureHandle noName = builder.createTexture({"", 1280, 720, Format::Rgba16Float});
        TextureHandle zeroWidth = builder.createTexture({"W", 0, 720, Format::Rgba16Float});
        TextureHandle unknownFormat = builder.createTexture({"F", 1280, 720, Format::Unknown});
        Check(noName.isNull() && zeroWidth.isNull() && unknownFormat.isNull(), "Invalid texture descriptors mint null handles");
        Check(builder.errors().size() == 3, "Each invalid texture descriptor records one error");
        Check(builder.errors()[0].category == ErrorCategory::InvalidName, "An empty texture name is InvalidName");
        Check(builder.errors()[1].category == ErrorCategory::InvalidDescriptor, "A zero extent is InvalidDescriptor");
        Check(builder.errors()[2].category == ErrorCategory::InvalidDescriptor, "An unknown format is InvalidDescriptor");
        Check(builder.resourceCount() == 0, "Invalid descriptors allocate no registry entry");
    }

    // Buffer descriptor validation
    {
        GraphBuilder builder;
        Check(builder.createBuffer({"", 16, 4}).isNull(), "An empty buffer name mints a null handle");
        Check(builder.createBuffer({"B", 0, 4}).isNull(), "Zero bytesPerElement mints a null handle");
        Check(builder.createBuffer({"B", 16, 0}).isNull(), "Zero numElements mints a null handle");
        Check(builder.errors().size() == 3, "Each invalid buffer descriptor records one error");
        Check(builder.errors()[0].category == ErrorCategory::InvalidName, "An empty buffer name is InvalidName");
        Check(builder.errors()[1].category == ErrorCategory::InvalidDescriptor, "Zero bytesPerElement is InvalidDescriptor");
        Check(builder.errors()[2].category == ErrorCategory::InvalidDescriptor, "Zero numElements is InvalidDescriptor");
    }

    // Valid creation stores the descriptor and flags
    {
        GraphBuilder builder;
        const TextureDesc desc{"HDRSceneColor", 1280, 720, Format::Rgba16Float};
        TextureHandle texture = builder.createTexture(desc);
        Check(!texture.isNull(), "A valid texture descriptor mints a handle");
        Check(builder.resourceCount() == 1, "One registry entry exists");
        const ResourceRecord& record = builder.resource(texture.index);
        Check(record.name == "HDRSceneColor", "The record keeps the descriptor name");
        Check(record.kind == ResourceKind::Texture, "The record is a texture");
        Check(!record.imported && !record.exported, "Created resources start unflagged");
        Check(record.currentVersion == 0, "Records start at version 0");
        const TextureDesc* stored = std::get_if<TextureDesc>(&record.desc);
        Check(stored != nullptr && *stored == desc, "The record stores the descriptor; equality is the pooling-key shape");
    }

    // import / export flags (UE bExternal / bExtracted semantics)
    {
        GraphBuilder builder;
        TextureHandle backBuffer = builder.importTexture({"BackBuffer", 1280, 720, Format::Srgba8Unorm});
        BufferHandle external = builder.importBuffer({"ExternalData", 16, 64});
        Check(builder.resource(backBuffer.index).imported, "importTexture sets Imported");
        Check(!builder.resource(backBuffer.index).exported, "Imported is not Exported");
        Check(builder.resource(external.index).imported, "importBuffer sets Imported");
        builder.exportTexture(backBuffer);
        Check(builder.resource(backBuffer.index).exported, "exportTexture sets Exported");
        builder.exportTexture(backBuffer);
        Check(
            builder.errors().empty() && builder.resource(backBuffer.index).exported,
            "Exporting twice stays legal and idempotent");
        builder.exportBuffer(BufferHandle{});
        Check(
            builder.errors().size() == 1 && builder.errors()[0].category == ErrorCategory::NullHandle,
            "Exporting a null handle is NullHandle");
    }

    // Descriptor equality (S5.7 pooling-key preparation)
    {
        const TextureDesc a{"T", 64, 64, Format::Rgba8Unorm};
        const TextureDesc same{"T", 64, 64, Format::Rgba8Unorm};
        const TextureDesc otherFormat{"T", 64, 64, Format::Rgba16Float};
        Check(a == same, "TextureDesc equality compares all fields");
        Check(!(a == otherFormat), "TextureDesc inequality on format");
        const BufferDesc ba{"B", 4, 16};
        const BufferDesc bb{"B", 4, 16};
        const BufferDesc bc{"B", 4, 32};
        Check(ba == bb && !(ba == bc), "BufferDesc equality compares element counts");
    }

    // addPass validation and flag handling
    {
        GraphBuilder builder;
        auto bad = builder.addPass("", PassFlags::Raster);
        Check(!bad.isValid(), "An empty pass name yields an invalid PassBuilder");
        bad.read(TextureHandle{});
        Check(builder.errors().size() == 1, "Declarations on an invalid PassBuilder are no-ops");
        Check(builder.passCount() == 0, "A rejected pass allocates no pass record");

        auto badFlags = builder.addPass("F", static_cast<PassFlags>(0x10));
        Check(!badFlags.isValid(), "Unknown flag bits yield an invalid PassBuilder");
        Check(builder.errors().size() == 2, "Unknown flag bits record one error");
        Check(builder.errors()[1].category == ErrorCategory::InvalidPassFlags, "Unknown flag bits are InvalidPassFlags");
        Check(builder.errors()[1].passName == "F", "The flag error names the pass");

        auto none = builder.addPass("NoFlags", PassFlags::None);
        Check(none.isValid() && builder.passCount() == 1, "PassFlags::None is allowed");
        Check(builder.pass(0).flags == PassFlags::None, "Flags are stored verbatim");
        auto combined = builder.addPass("RasterNeverCull", PassFlags::Raster | PassFlags::NeverCull);
        Check(
            combined.isValid() && builder.pass(1).flags == (PassFlags::Raster | PassFlags::NeverCull),
            "Raster | NeverCull combines");
    }

    // Per-declaration rejection: a valid declaration still records next to a
    // rejected one
    {
        GraphBuilder builder;
        TextureHandle good = builder.createTexture({"Good", 8, 8, Format::Rgba8Unorm});
        auto pass = builder.addPass("Mixed", PassFlags::Raster);
        pass.read(TextureHandle{});
        pass.write(good);
        Check(builder.errors().size() == 1, "Only the invalid declaration errors");
        Check(builder.pass(0).accesses.size() == 1, "Only the valid declaration is recorded");
        Check(builder.pass(0).accesses[0].mode == AccessMode::Write, "The surviving declaration is the write");
    }

    // Declaration scopes stay bound to their own pass when interleaved
    {
        GraphBuilder builder;
        TextureHandle texture = builder.createTexture({"T", 8, 8, Format::Rgba8Unorm});
        auto first = builder.addPass("First", PassFlags::Raster);
        auto second = builder.addPass("Second", PassFlags::NeverCull);
        first.read(texture);
        second.write(texture);
        Check(
            builder.pass(0).accesses.size() == 1 && builder.pass(0).accesses[0].mode == AccessMode::Read,
            "An older PassBuilder still targets its own pass");
        Check(
            builder.pass(1).accesses.size() == 1 && builder.pass(1).accesses[0].mode == AccessMode::Write,
            "The newer PassBuilder targets its own pass");
    }

    // The M1-shaped graph: the frozen manual pipeline (docs/m1-reference.md)
    // expressed as pure declarations, no GPU device
    {
        GraphBuilder builder;

        TextureHandle backBuffer = builder.importTexture({"BackBuffer", 1280, 720, Format::Srgba8Unorm});
        TextureHandle gbufferA = builder.createTexture({"GBufferA", 1280, 720, Format::Srgba8Unorm});
        TextureHandle gbufferB = builder.createTexture({"GBufferB", 1280, 720, Format::Rgba16Float});
        TextureHandle gbufferC = builder.createTexture({"GBufferC", 1280, 720, Format::Rgba8Unorm});
        TextureHandle gbufferDepth = builder.createTexture({"GBufferDepth", 1280, 720, Format::D32Float});
        TextureHandle hdrSceneColor = builder.createTexture({"HDRSceneColor", 1280, 720, Format::Rgba16Float});

        auto gbufferPass = builder.addPass("GBuffer", PassFlags::Raster);
        gbufferPass.write(gbufferA);
        gbufferPass.write(gbufferB);
        gbufferPass.write(gbufferC);
        gbufferPass.write(gbufferDepth);

        auto deferredPass = builder.addPass("DeferredLighting", PassFlags::Raster);
        deferredPass.read(gbufferA);
        deferredPass.read(gbufferB);
        deferredPass.read(gbufferC);
        deferredPass.read(gbufferDepth);
        deferredPass.write(hdrSceneColor);

        auto postPass = builder.addPass("PostProcess", PassFlags::Raster);
        postPass.read(hdrSceneColor);
        postPass.write(backBuffer);

        builder.exportTexture(backBuffer);

        Check(builder.errors().empty(), "The M1-shaped graph constructs without errors");
        Check(builder.resourceCount() == 6, "The M1-shaped graph has six resources");
        Check(builder.passCount() == 3, "The M1-shaped graph has three passes");
        Check(
            builder.pass(0).name == "GBuffer" && builder.pass(0).accesses.size() == 4,
            "GBuffer declares four writes");
        Check(
            builder.pass(1).name == "DeferredLighting" && builder.pass(1).accesses.size() == 5,
            "DeferredLighting declares four reads and one write");
        Check(
            builder.pass(2).name == "PostProcess" && builder.pass(2).accesses.size() == 2,
            "PostProcess declares one read and one write");
        Check(
            builder.resource(backBuffer.index).imported && builder.resource(backBuffer.index).exported,
            "The back buffer is imported and exported (cull root)");
        Check(
            !builder.resource(gbufferA.index).imported && !builder.resource(gbufferA.index).exported,
            "Internal GBuffer targets carry no root flags");
        builder.assertNoErrors();
        Check(true, "The M1-shaped graph passes assertNoErrors");
    }

    return g_failures;
}
