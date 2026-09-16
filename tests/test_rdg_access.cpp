#include "rdg/GraphCompiler.h"
#include "rdg/M1ShapedGraph.h"
#include "rdg/Pass.h"
#include "rdg/ResourceDesc.h"
#include "rdg/exec/AccessMap.h"
#include "rdg/exec/FormatMap.h"
#include "rdg/exec/GraphExecutor.h"

#include <cstdint>
#include <cstdio>

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

    void CheckFormatMapAgreement(Format format, const char* message)
    {
        const TextureDesc desc{"t", 8, 8, format};
        const Access initial = InferAccess(AccessMode::Write, ResourceKind::Texture, format);
        Check(ToNvStates(initial) == MakeTextureDesc(desc).initialState, message);
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
    }

    return g_failures;
}
