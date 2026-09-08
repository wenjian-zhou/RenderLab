#include "rdg/GraphBuilder.h"

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

    bool AllOfCategory(std::span<const Error> errors, ErrorCategory category)
    {
        if (errors.empty())
        {
            return false;
        }
        for (const Error& error : errors)
        {
            if (error.category != category)
            {
                return false;
            }
        }
        return true;
    }

    TextureDesc MakeTextureDesc(const char* name)
    {
        return TextureDesc{name, 1280, 720, Format::Rgba16Float};
    }

    BufferDesc MakeBufferDesc(const char* name)
    {
        return BufferDesc{name, 16, 256};
    }
}

int RunRdgHandleTests()
{
    std::printf("RenderLab S4.1 RDG handle tests\n");

    // Graph identity
    {
        GraphBuilder a;
        GraphBuilder b;
        Check(a.graphId() != b.graphId(), "Two builders get distinct graph ids");
        Check(a.graphId() != 0 && b.graphId() != 0, "Graph ids never use the reserved value 0");
    }

    // Minted handles
    {
        GraphBuilder builder;
        TextureHandle texture = builder.createTexture(MakeTextureDesc("T"));
        Check(!texture.isNull(), "A created texture handle is not null");
        Check(texture.version == 0, "A created handle carries version 0");
        Check(texture.graphId == builder.graphId(), "A created handle carries the owning graph id");

        TextureHandle nullTexture;
        BufferHandle nullBuffer;
        Check(nullTexture.isNull() && nullBuffer.isNull(), "Default-constructed handles are null");
        Check(nullTexture == TextureHandle{}, "Null texture handles compare equal");
        Check(texture != TextureHandle{}, "A minted handle differs from the null handle");
    }

    // Null handles fail deterministically at every entry point
    {
        GraphBuilder builder;
        builder.createTexture(MakeTextureDesc("T"));
        auto pass = builder.addPass("P", PassFlags::Raster);
        pass.read(TextureHandle{});
        pass.write(BufferHandle{});
        builder.exportTexture(TextureHandle{});
        Check(builder.errors().size() == 3, "Null-handle use records one error per call");
        Check(AllOfCategory(builder.errors(), ErrorCategory::NullHandle), "Every null use is NullHandle");
        Check(builder.errors()[0].passName == "P", "A pass-scoped null error names the pass");
        Check(builder.pass(0).accesses.empty(), "Null declarations are rejected, not recorded");
    }

    // Cross-graph use fails deterministically
    {
        GraphBuilder a;
        GraphBuilder b;
        TextureHandle fromA = a.createTexture(MakeTextureDesc("A.T"));
        auto passInB = b.addPass("PassInB", PassFlags::Raster);
        passInB.read(fromA);
        b.exportTexture(fromA);
        Check(b.errors().size() == 2, "Both cross-graph uses record errors");
        Check(AllOfCategory(b.errors(), ErrorCategory::ForeignGraph), "Cross-graph use is ForeignGraph");
        Check(b.pass(0).accesses.empty(), "Cross-graph declaration is rejected");
        Check(a.errors().empty(), "The owning builder records no error");
        Check(b.errors()[0].passName == "PassInB", "The cross-graph error names the declaring pass");
    }

    // Stale version (forged): S4.1 has no write that bumps a version, so the
    // mechanism is exercised with a forged mismatch.
    {
        GraphBuilder builder;
        TextureHandle texture = builder.createTexture(MakeTextureDesc("T"));
        TextureHandle stale = texture;
        stale.version = 7;
        auto pass = builder.addPass("P", PassFlags::Raster);
        pass.read(stale);
        Check(AllOfCategory(builder.errors(), ErrorCategory::StaleVersion), "Version mismatch is StaleVersion");
        Check(builder.errors()[0].resourceName == "T", "The stale error names the resource");
        Check(builder.errors()[0].passName == "P", "The stale error names the pass");
        Check(builder.pass(0).accesses.empty(), "The stale declaration is rejected");
    }

    // Out-of-range index (forged with the correct graph id)
    {
        GraphBuilder builder;
        builder.createTexture(MakeTextureDesc("T"));
        TextureHandle forged{99, 0, builder.graphId()};
        auto pass = builder.addPass("P", PassFlags::Raster);
        pass.write(forged);
        Check(AllOfCategory(builder.errors(), ErrorCategory::StaleVersion), "Out-of-range index is StaleVersion");
        Check(builder.pass(0).accesses.empty(), "The out-of-range declaration is rejected");
    }

    // Type mismatch (forged): a buffer handle over a texture slot, and the
    // reverse
    {
        GraphBuilder builder;
        TextureHandle texture = builder.createTexture(MakeTextureDesc("T"));
        BufferHandle forgedAsBuffer{texture.index, 0, builder.graphId()};
        auto pass = builder.addPass("P", PassFlags::Raster);
        pass.read(forgedAsBuffer);
        Check(AllOfCategory(builder.errors(), ErrorCategory::TypeMismatch), "A buffer handle over a texture slot is TypeMismatch");
        Check(builder.errors()[0].resourceName == "T", "The type-mismatch error names the target resource");
    }
    {
        GraphBuilder builder;
        BufferHandle buffer = builder.createBuffer(MakeBufferDesc("B"));
        TextureHandle forgedAsTexture{buffer.index, 0, builder.graphId()};
        auto pass = builder.addPass("P", PassFlags::Raster);
        pass.write(forgedAsTexture);
        Check(AllOfCategory(builder.errors(), ErrorCategory::TypeMismatch), "A texture handle over a buffer slot is TypeMismatch");
    }

    // Validation order: null beats owning-graph, owning-graph beats kind and
    // version — one error per bad handle, earliest category wins
    {
        GraphBuilder builder;
        TextureHandle texture = builder.createTexture(MakeTextureDesc("T"));

        BufferHandle foreignAndMismatched{texture.index, 9, texture.graphId + 1u};
        auto pass = builder.addPass("P", PassFlags::Raster);
        pass.read(foreignAndMismatched);
        Check(AllOfCategory(builder.errors(), ErrorCategory::ForeignGraph), "Foreign graph is checked before kind and version");

        TextureHandle nullWithBogusGraph{kNullHandleIndex, 3, 999};
        pass.write(nullWithBogusGraph);
        Check(
            builder.errors().size() == 2 && builder.errors()[1].category == ErrorCategory::NullHandle,
            "Null is checked before owning graph");
    }

    // Happy path: declarations are recorded verbatim
    {
        GraphBuilder builder;
        TextureHandle texture = builder.createTexture(MakeTextureDesc("T"));
        BufferHandle buffer = builder.createBuffer(MakeBufferDesc("B"));
        auto pass = builder.addPass("P", PassFlags::Raster);
        pass.read(texture);
        pass.write(texture);
        pass.read(texture); // duplicate: S4.1 records verbatim
        pass.read(buffer);
        Check(builder.errors().empty(), "Valid declarations record no errors");
        Check(builder.pass(0).accesses.size() == 4, "Declarations are recorded verbatim, including duplicates");
        const ResourceAccess& first = builder.pass(0).accesses[0];
        Check(
            first.kind == ResourceKind::Texture && first.index == texture.index && first.version == 0 &&
                first.mode == AccessMode::Read,
            "An access record pins kind, index, version, and mode");
        Check(builder.pass(0).accesses[1].mode == AccessMode::Write, "Read and write of the same resource both record");
        Check(
            builder.pass(0).accesses[3].kind == ResourceKind::Buffer && builder.pass(0).accesses[3].index == buffer.index,
            "A buffer access records against the same registry");
        builder.assertNoErrors();
        Check(true, "assertNoErrors passes on a clean builder");
    }

    return g_failures;
}
