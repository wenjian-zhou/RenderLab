#include "rdg/GraphCompiler.h"
#include "rdg/exec/GraphExecutor.h"

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

    return g_failures;
}
