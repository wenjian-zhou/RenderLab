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
        TextureHandle noName = builder.CreateTexture({"", 1280, 720, Format::Rgba16Float});
        TextureHandle zeroWidth = builder.CreateTexture({"W", 0, 720, Format::Rgba16Float});
        TextureHandle unknownFormat = builder.CreateTexture({"F", 1280, 720, Format::Unknown});
        Check(noName.IsNull() && zeroWidth.IsNull() && unknownFormat.IsNull(), "Invalid texture descriptors mint null handles");
        Check(builder.GetErrors().size() == 3, "Each invalid texture descriptor records one error");
        Check(builder.GetErrors()[0].category == ErrorCategory::InvalidName, "An empty texture name is InvalidName");
        Check(builder.GetErrors()[1].category == ErrorCategory::InvalidDescriptor, "A zero extent is InvalidDescriptor");
        Check(builder.GetErrors()[2].category == ErrorCategory::InvalidDescriptor, "An unknown format is InvalidDescriptor");
        Check(builder.GetResourceCount() == 0, "Invalid descriptors allocate no registry entry");
    }

    // Buffer descriptor validation
    {
        GraphBuilder builder;
        Check(builder.CreateBuffer({"", 16, 4}).IsNull(), "An empty buffer name mints a null handle");
        Check(builder.CreateBuffer({"B", 0, 4}).IsNull(), "Zero bytesPerElement mints a null handle");
        Check(builder.CreateBuffer({"B", 16, 0}).IsNull(), "Zero numElements mints a null handle");
        Check(builder.GetErrors().size() == 3, "Each invalid buffer descriptor records one error");
        Check(builder.GetErrors()[0].category == ErrorCategory::InvalidName, "An empty buffer name is InvalidName");
        Check(builder.GetErrors()[1].category == ErrorCategory::InvalidDescriptor, "Zero bytesPerElement is InvalidDescriptor");
        Check(builder.GetErrors()[2].category == ErrorCategory::InvalidDescriptor, "Zero numElements is InvalidDescriptor");
    }

    // Valid creation stores the descriptor and flags
    {
        GraphBuilder builder;
        const TextureDesc desc{"HDRSceneColor", 1280, 720, Format::Rgba16Float};
        TextureHandle texture = builder.CreateTexture(desc);
        Check(!texture.IsNull(), "A valid texture descriptor mints a handle");
        Check(builder.GetResourceCount() == 1, "One registry entry exists");
        const ResourceRecord& record = builder.GetResource(texture.index);
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
        TextureHandle backBuffer = builder.ImportTexture({"BackBuffer", 1280, 720, Format::Srgba8Unorm});
        BufferHandle external = builder.ImportBuffer({"ExternalData", 16, 64});
        Check(builder.GetResource(backBuffer.index).imported, "ImportTexture sets Imported");
        Check(!builder.GetResource(backBuffer.index).exported, "Imported is not Exported");
        Check(builder.GetResource(external.index).imported, "ImportBuffer sets Imported");
        builder.ExportTexture(backBuffer);
        Check(builder.GetResource(backBuffer.index).exported, "ExportTexture sets Exported");
        builder.ExportTexture(backBuffer);
        Check(
            builder.GetErrors().empty() && builder.GetResource(backBuffer.index).exported,
            "Exporting twice stays legal and idempotent");
        builder.ExportBuffer(BufferHandle{});
        Check(
            builder.GetErrors().size() == 1 && builder.GetErrors()[0].category == ErrorCategory::NullHandle,
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

    // AddPass validation and flag handling
    {
        GraphBuilder builder;
        auto bad = builder.AddPass("", PassFlags::Raster);
        Check(!bad.IsValid(), "An empty pass name yields an invalid PassBuilder");
        bad.Read(TextureHandle{});
        Check(builder.GetErrors().size() == 1, "Declarations on an invalid PassBuilder are no-ops");
        Check(builder.GetPassCount() == 0, "A rejected pass allocates no pass record");

        auto badFlags = builder.AddPass("F", static_cast<PassFlags>(0x10));
        Check(!badFlags.IsValid(), "Unknown flag bits yield an invalid PassBuilder");
        Check(builder.GetErrors().size() == 2, "Unknown flag bits record one error");
        Check(builder.GetErrors()[1].category == ErrorCategory::InvalidPassFlags, "Unknown flag bits are InvalidPassFlags");
        Check(builder.GetErrors()[1].passName == "F", "The flag error names the pass");

        auto none = builder.AddPass("NoFlags", PassFlags::None);
        Check(none.IsValid() && builder.GetPassCount() == 1, "PassFlags::None is allowed");
        Check(builder.GetPass(0).flags == PassFlags::None, "Flags are stored verbatim");
        auto combined = builder.AddPass("RasterNeverCull", PassFlags::Raster | PassFlags::NeverCull);
        Check(
            combined.IsValid() && builder.GetPass(1).flags == (PassFlags::Raster | PassFlags::NeverCull),
            "Raster | NeverCull combines");
    }

    // Per-declaration rejection: a valid declaration still records next to a
    // rejected one
    {
        GraphBuilder builder;
        TextureHandle good = builder.CreateTexture({"Good", 8, 8, Format::Rgba8Unorm});
        auto pass = builder.AddPass("Mixed", PassFlags::Raster);
        pass.Read(TextureHandle{});
        pass.Write(good);
        Check(builder.GetErrors().size() == 1, "Only the invalid declaration errors");
        Check(builder.GetPass(0).accesses.size() == 1, "Only the valid declaration is recorded");
        Check(builder.GetPass(0).accesses[0].mode == AccessMode::Write, "The surviving declaration is the write");
    }

    // Declaration scopes stay bound to their own pass when interleaved
    {
        GraphBuilder builder;
        TextureHandle texture = builder.CreateTexture({"T", 8, 8, Format::Rgba8Unorm});
        auto first = builder.AddPass("First", PassFlags::Raster);
        auto second = builder.AddPass("Second", PassFlags::NeverCull);
        first.Read(texture);
        second.Write(texture);
        Check(
            builder.GetPass(0).accesses.size() == 1 && builder.GetPass(0).accesses[0].mode == AccessMode::Read,
            "An older PassBuilder still targets its own pass");
        Check(
            builder.GetPass(1).accesses.size() == 1 && builder.GetPass(1).accesses[0].mode == AccessMode::Write,
            "The newer PassBuilder targets its own pass");
    }

    // The M1-shaped graph: the frozen manual pipeline (docs/m1-reference.md)
    // expressed as pure declarations, no GPU device
    {
        GraphBuilder builder;

        TextureHandle backBuffer = builder.ImportTexture({"BackBuffer", 1280, 720, Format::Srgba8Unorm});
        TextureHandle gbufferA = builder.CreateTexture({"GBufferA", 1280, 720, Format::Srgba8Unorm});
        TextureHandle gbufferB = builder.CreateTexture({"GBufferB", 1280, 720, Format::Rgba16Float});
        TextureHandle gbufferC = builder.CreateTexture({"GBufferC", 1280, 720, Format::Rgba8Unorm});
        TextureHandle gbufferDepth = builder.CreateTexture({"GBufferDepth", 1280, 720, Format::D32Float});
        TextureHandle hdrSceneColor = builder.CreateTexture({"HDRSceneColor", 1280, 720, Format::Rgba16Float});

        auto gbufferPass = builder.AddPass("GBuffer", PassFlags::Raster);
        gbufferPass.Write(gbufferA);
        gbufferPass.Write(gbufferB);
        gbufferPass.Write(gbufferC);
        gbufferPass.Write(gbufferDepth);

        auto deferredPass = builder.AddPass("DeferredLighting", PassFlags::Raster);
        deferredPass.Read(gbufferA);
        deferredPass.Read(gbufferB);
        deferredPass.Read(gbufferC);
        deferredPass.Read(gbufferDepth);
        deferredPass.Write(hdrSceneColor);

        auto postPass = builder.AddPass("PostProcess", PassFlags::Raster);
        postPass.Read(hdrSceneColor);
        postPass.Write(backBuffer);

        builder.ExportTexture(backBuffer);

        Check(builder.GetErrors().empty(), "The M1-shaped graph constructs without errors");
        Check(builder.GetResourceCount() == 6, "The M1-shaped graph has six resources");
        Check(builder.GetPassCount() == 3, "The M1-shaped graph has three passes");
        Check(
            builder.GetPass(0).name == "GBuffer" && builder.GetPass(0).accesses.size() == 4,
            "GBuffer declares four writes");
        Check(
            builder.GetPass(1).name == "DeferredLighting" && builder.GetPass(1).accesses.size() == 5,
            "DeferredLighting declares four reads and one write");
        Check(
            builder.GetPass(2).name == "PostProcess" && builder.GetPass(2).accesses.size() == 2,
            "PostProcess declares one read and one write");
        Check(
            builder.GetResource(backBuffer.index).imported && builder.GetResource(backBuffer.index).exported,
            "The back buffer is imported and exported (cull root)");
        Check(
            !builder.GetResource(gbufferA.index).imported && !builder.GetResource(gbufferA.index).exported,
            "Internal GBuffer targets carry no root flags");
        builder.AssertNoErrors();
        Check(true, "The M1-shaped graph passes AssertNoErrors");
    }

    return g_failures;
}
