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
        return TextureDesc{name, 1280, 720, Format::RGBA16Float};
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
        pass.Use(TextureHandle{}, Access::ShaderResource);
        pass.Use(BufferHandle{}, Access::RenderTarget);
        builder.ExportTexture(TextureHandle{}, Access::RenderTarget);
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
        passInB.Use(fromA, Access::ShaderResource);
        b.ExportTexture(fromA, Access::RenderTarget);
        Check(b.GetErrors().size() == 2, "Both cross-graph uses record errors");
        Check(AllOfCategory(b.GetErrors(), ErrorCategory::ForeignGraph), "Cross-graph use is ForeignGraph");
        Check(b.GetPass(0).accesses.empty(), "Cross-graph declaration is rejected");
        Check(a.GetErrors().empty(), "The owning builder records no error");
        Check(b.GetErrors()[0].passName == "PassInB", "The cross-graph error names the declaring pass");
    }

    {
        GraphBuilder builder;
        TextureHandle texture = builder.CreateTexture(MakeTextureDesc("T"));
        builder.SetInitialAccess(texture, Access::Present);
        Check(AllOfCategory(builder.GetErrors(), ErrorCategory::IncompatibleAccess),
            "SetInitialAccess on Create* is IncompatibleAccess");
        Check(builder.GetErrors()[0].resourceName == "T", "The initial-access error names the resource");
    }

    // Out-of-range index (forged with the correct graph id)
    {
        GraphBuilder builder;
        builder.CreateTexture(MakeTextureDesc("T"));
        TextureHandle forged{99, builder.GetGraphId()};
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Use(forged, Access::RenderTarget);
        Check(AllOfCategory(builder.GetErrors(), ErrorCategory::IncompatibleAccess), "Out-of-range index is IncompatibleAccess");
        Check(builder.GetPass(0).accesses.empty(), "The out-of-range declaration is rejected");
    }

    // Type mismatch (forged): a buffer handle over a texture slot, and the
    // reverse
    {
        GraphBuilder builder;
        TextureHandle texture = builder.CreateTexture(MakeTextureDesc("T"));
        BufferHandle forgedAsBuffer{texture.index, builder.GetGraphId()};
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Use(forgedAsBuffer, Access::ShaderResource);
        Check(AllOfCategory(builder.GetErrors(), ErrorCategory::TypeMismatch), "A buffer handle over a texture slot is TypeMismatch");
        Check(builder.GetErrors()[0].resourceName == "T", "The type-mismatch error names the target resource");
    }
    {
        GraphBuilder builder;
        BufferHandle buffer = builder.CreateBuffer(MakeBufferDesc("B"));
        TextureHandle forgedAsTexture{buffer.index, builder.GetGraphId()};
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Use(forgedAsTexture, Access::RenderTarget);
        Check(AllOfCategory(builder.GetErrors(), ErrorCategory::TypeMismatch), "A texture handle over a buffer slot is TypeMismatch");
    }

    // Validation order: null beats owning-graph, owning-graph beats kind and
    // version — one error per bad handle, earliest category wins
    {
        GraphBuilder builder;
        TextureHandle texture = builder.CreateTexture(MakeTextureDesc("T"));

        BufferHandle foreignAndMismatched{texture.index, texture.graphId + 1u};
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Use(foreignAndMismatched, Access::ShaderResource);
        Check(AllOfCategory(builder.GetErrors(), ErrorCategory::ForeignGraph), "Foreign graph is checked before kind and version");

        TextureHandle nullWithBogusGraph{kNullHandleIndex, 999};
        pass.Use(nullWithBogusGraph, Access::RenderTarget);
        Check(
            builder.GetErrors().size() == 2 && builder.GetErrors()[1].category == ErrorCategory::NullHandle,
            "Null is checked before owning graph");
    }

    {
        GraphBuilder builder;
        TextureHandle texture = builder.ImportTexture(MakeTextureDesc("T"));
        BufferHandle buffer = builder.ImportBuffer(MakeBufferDesc("B"));
        auto pass = builder.AddPass("P", PassFlags::Raster);
        pass.Use(texture, Access::ShaderResource);
        pass.Use(texture, Access::ShaderResource);
        pass.Use(buffer, Access::ShaderResource);
        Check(builder.GetErrors().empty(), "Valid declarations record no errors");
        Check(builder.GetPass(0).accesses.size() == 3, "Read declarations are recorded verbatim, including duplicates");
        const ResourceAccess& first = builder.GetPass(0).accesses[0];
        Check(
            first.kind == ResourceKind::Texture && first.index == texture.index &&
                first.access == Access::ShaderResource,
            "An access record pins kind, index, and access");
        Check(builder.GetPass(0).accesses[1].index == texture.index,
            "A second ShaderResource use of the same resource is recorded");
        Check(
            builder.GetPass(0).accesses[2].kind == ResourceKind::Buffer && builder.GetPass(0).accesses[2].index == buffer.index,
            "A buffer access records against the same registry");
        builder.AssertNoErrors();
        Check(true, "AssertNoErrors passes on a clean builder");
    }

    return g_failures;
}
