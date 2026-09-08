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
        Check(a.GetGraphId() != b.GetGraphId(), "Two builders get distinct graph ids");
        Check(a.GetGraphId() != 0 && b.GetGraphId() != 0, "Graph ids never use the reserved value 0");
    }

    // Minted handles
    {
        GraphBuilder builder;
        TextureHandle texture = builder.CreateTexture(MakeTextureDesc("T"));
        Check(!texture.IsNull(), "A created texture handle is not null");
        Check(texture.version == 0, "A created handle carries version 0");
        Check(texture.graphId == builder.GetGraphId(), "A created handle carries the owning graph id");

        TextureHandle nullTexture;
        BufferHandle nullBuffer;
        Check(nullTexture.IsNull() && nullBuffer.IsNull(), "Default-constructed handles are null");
        Check(nullTexture == TextureHandle{}, "Null texture handles compare equal");
        Check(texture != TextureHandle{}, "A minted handle differs from the null handle");
    }

    // Null handles fail deterministically at every entry point
    {
        GraphBuilder builder;
        builder.CreateTexture(MakeTextureDesc("T"));
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Read(TextureHandle{});
        pass.Write(BufferHandle{});
        builder.ExportTexture(TextureHandle{});
        Check(builder.GetErrors().size() == 3, "Null-handle use records one error per call");
        Check(AllOfCategory(builder.GetErrors(), ErrorCategory::NullHandle), "Every null use is NullHandle");
        Check(builder.GetErrors()[0].passName == "P", "A pass-scoped null error names the pass");
        Check(builder.GetPass(0).accesses.empty(), "Null declarations are rejected, not recorded");
    }

    // Cross-graph use fails deterministically
    {
        GraphBuilder a;
        GraphBuilder b;
        TextureHandle fromA = a.CreateTexture(MakeTextureDesc("A.T"));
        auto passInB = b.AddPass("PassInB", PassFlags::Raster);
        passInB.Read(fromA);
        b.ExportTexture(fromA);
        Check(b.GetErrors().size() == 2, "Both cross-graph uses record errors");
        Check(AllOfCategory(b.GetErrors(), ErrorCategory::ForeignGraph), "Cross-graph use is ForeignGraph");
        Check(b.GetPass(0).accesses.empty(), "Cross-graph declaration is rejected");
        Check(a.GetErrors().empty(), "The owning builder records no error");
        Check(b.GetErrors()[0].passName == "PassInB", "The cross-graph error names the declaring pass");
    }

    // Stale version (forged): S4.1 has no write that bumps a version, so the
    // mechanism is exercised with a forged mismatch.
    {
        GraphBuilder builder;
        TextureHandle texture = builder.CreateTexture(MakeTextureDesc("T"));
        TextureHandle stale = texture;
        stale.version = 7;
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Read(stale);
        Check(AllOfCategory(builder.GetErrors(), ErrorCategory::StaleVersion), "Version mismatch is StaleVersion");
        Check(builder.GetErrors()[0].resourceName == "T", "The stale error names the resource");
        Check(builder.GetErrors()[0].passName == "P", "The stale error names the pass");
        Check(builder.GetPass(0).accesses.empty(), "The stale declaration is rejected");
    }

    // Out-of-range index (forged with the correct graph id)
    {
        GraphBuilder builder;
        builder.CreateTexture(MakeTextureDesc("T"));
        TextureHandle forged{99, 0, builder.GetGraphId()};
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Write(forged);
        Check(AllOfCategory(builder.GetErrors(), ErrorCategory::StaleVersion), "Out-of-range index is StaleVersion");
        Check(builder.GetPass(0).accesses.empty(), "The out-of-range declaration is rejected");
    }

    // Type mismatch (forged): a buffer handle over a texture slot, and the
    // reverse
    {
        GraphBuilder builder;
        TextureHandle texture = builder.CreateTexture(MakeTextureDesc("T"));
        BufferHandle forgedAsBuffer{texture.index, 0, builder.GetGraphId()};
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Read(forgedAsBuffer);
        Check(AllOfCategory(builder.GetErrors(), ErrorCategory::TypeMismatch), "A buffer handle over a texture slot is TypeMismatch");
        Check(builder.GetErrors()[0].resourceName == "T", "The type-mismatch error names the target resource");
    }
    {
        GraphBuilder builder;
        BufferHandle buffer = builder.CreateBuffer(MakeBufferDesc("B"));
        TextureHandle forgedAsTexture{buffer.index, 0, builder.GetGraphId()};
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Write(forgedAsTexture);
        Check(AllOfCategory(builder.GetErrors(), ErrorCategory::TypeMismatch), "A texture handle over a buffer slot is TypeMismatch");
    }

    // Validation order: null beats owning-graph, owning-graph beats kind and
    // version — one error per bad handle, earliest category wins
    {
        GraphBuilder builder;
        TextureHandle texture = builder.CreateTexture(MakeTextureDesc("T"));

        BufferHandle foreignAndMismatched{texture.index, 9, texture.graphId + 1u};
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Read(foreignAndMismatched);
        Check(AllOfCategory(builder.GetErrors(), ErrorCategory::ForeignGraph), "Foreign graph is checked before kind and version");

        TextureHandle nullWithBogusGraph{kNullHandleIndex, 3, 999};
        pass.Write(nullWithBogusGraph);
        Check(
            builder.GetErrors().size() == 2 && builder.GetErrors()[1].category == ErrorCategory::NullHandle,
            "Null is checked before owning graph");
    }

    // Happy path: declarations are recorded verbatim
    {
        GraphBuilder builder;
        TextureHandle texture = builder.CreateTexture(MakeTextureDesc("T"));
        BufferHandle buffer = builder.CreateBuffer(MakeBufferDesc("B"));
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Read(texture);
        pass.Write(texture);
        pass.Read(texture); // duplicate: S4.1 records verbatim
        pass.Read(buffer);
        Check(builder.GetErrors().empty(), "Valid declarations record no errors");
        Check(builder.GetPass(0).accesses.size() == 4, "Declarations are recorded verbatim, including duplicates");
        const ResourceAccess& first = builder.GetPass(0).accesses[0];
        Check(
            first.kind == ResourceKind::Texture && first.index == texture.index && first.version == 0 &&
                first.mode == AccessMode::Read,
            "An access record pins kind, index, version, and mode");
        Check(builder.GetPass(0).accesses[1].mode == AccessMode::Write, "Read and write of the same resource both record");
        Check(
            builder.GetPass(0).accesses[3].kind == ResourceKind::Buffer && builder.GetPass(0).accesses[3].index == buffer.index,
            "A buffer access records against the same registry");
        builder.AssertNoErrors();
        Check(true, "AssertNoErrors passes on a clean builder");
    }

    return g_failures;
}
