#include "rdg/Pass.h"

#include <cstdint>
#include <cstdio>

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

int RunRdgAccessTests()
{
    std::printf("RenderLab S5.3 RDG access tests\n");

    Check(static_cast<uint32_t>(Access::Unknown) == 0, "Unknown is 0");
    Check((Access::ShaderResource | Access::RenderTarget) != Access::ShaderResource, "Access is a flag enum");
    Check((kKnownAccessBits & Access::Present) == Access::Present, "Present is in kKnownAccessBits");
    Check((kWritableMask & kReadableMask) == Access::Unknown, "read/write masks do not overlap");

    return g_failures;
}
