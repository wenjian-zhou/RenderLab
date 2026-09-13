#include "rdg/GraphCompiler.h"
#include "rdg/M1ShapedGraph.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace renderlab::rdg;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* message)
    {
        std::printf("  %s  %s\n", condition ? "PASS" : "FAIL", message);
        if (!condition)
        {
            ++g_failures;
        }
    }

    TextureHandle MakeTexture(GraphBuilder& graph, const char* name)
    {
        return graph.CreateTexture(TextureDesc{name, 8, 8, Format::RGBA8Unorm});
    }

    std::filesystem::path FindRdgGoldenDirectory()
    {
#ifdef RENDERLAB_SOURCE_DIR
        const std::filesystem::path fromSource =
            std::filesystem::path(RENDERLAB_SOURCE_DIR) / "tests" / "golden" / "rdg";
        if (std::filesystem::exists(fromSource / "rdg.txt") && std::filesystem::exists(fromSource / "rdg.dot"))
        {
            return fromSource;
        }
#endif
        std::filesystem::path current = std::filesystem::current_path();
        for (int i = 0; i < 8; ++i)
        {
            const std::filesystem::path candidate = current / "tests" / "golden" / "rdg";
            if (std::filesystem::exists(candidate / "rdg.txt") && std::filesystem::exists(candidate / "rdg.dot"))
            {
                return candidate;
            }
            if (!current.has_parent_path() || current == current.parent_path())
            {
                break;
            }
            current = current.parent_path();
        }
        return {};
    }

    std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input)
        {
            return {};
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }
}

int RunRdgDumpTests()
{
    std::printf("RenderLab S4.6 dump tests\n");

    {
        GraphBuilder graph;
        BuildM1ShapedGraph(graph);
        const CompileResult result = GraphCompiler::Compile(graph);
        const std::filesystem::path goldenDir = FindRdgGoldenDirectory();
        if (goldenDir.empty())
        {
            Check(false, "Committed goldens are present under tests/golden/rdg");
        }
        else
        {
            const std::string text = ReadFile(goldenDir / "rdg.txt");
            const std::string dot = ReadFile(goldenDir / "rdg.dot");
            Check(result.IsSuccess() && result.Dump() == text, "M1-shaped compile dump matches rdg.txt");
            Check(result.DumpDot() == dot, "M1-shaped DOT dump matches rdg.dot");
        }
        Check(result.Dump() == GraphCompiler::Compile(graph).Dump() &&
                result.DumpDot() == GraphCompiler::Compile(graph).DumpDot(),
            "Compiled dumps are deterministic");
    }

    {
        GraphBuilder graph;
        TextureHandle first = MakeTexture(graph, "First");
        TextureHandle second = MakeTexture(graph, "Second");
        auto passA = graph.AddPass("A", PassFlags::Raster);
        auto passB = graph.AddPass("B", PassFlags::Raster);
        first = passA.Write(first);
        second = passB.Write(second);
        passA.Read(second);
        passB.Read(first);
        const std::string dump = GraphCompiler::Compile(graph).Dump();
        Check(dump.find("cycle:") != std::string::npos && dump.find("\"A\"") != std::string::npos &&
                dump.find("\"B\"") != std::string::npos && dump.find("\"First\"") != std::string::npos &&
                dump.find("\"Second\"") != std::string::npos && dump.find("cull: skipped") != std::string::npos &&
                dump.find("lifetime: skipped") != std::string::npos && dump.find("flags:") != std::string::npos &&
                dump.find("accesses:") != std::string::npos && dump.find("versions:") != std::string::npos,
            "Cycle dump names involved passes and resources");
    }

    {
        GraphBuilder graph;
        auto pass = graph.AddPass("Invalid", PassFlags::Raster);
        pass.Read(TextureHandle{});
        const std::string dump = GraphCompiler::Compile(graph).Dump();
        Check(dump.find("NullHandle") != std::string::npos && dump.find("pass=0") != std::string::npos &&
                dump.find("\"Invalid\"") != std::string::npos && dump.find("resource=") != std::string::npos &&
                dump.find("cull: skipped") != std::string::npos && dump.find("lifetime: skipped") != std::string::npos &&
                dump.find("flags:") != std::string::npos && dump.find("accesses:") != std::string::npos &&
                dump.find("versions:") != std::string::npos,
            "Builder error dump names pass and resource and skips cull/lifetime");
    }

    {
        GraphBuilder graph;
        TextureHandle output = MakeTexture(graph, "Output");
        TextureHandle unused = MakeTexture(graph, "Unused");
        output = graph.AddPass("Keep", PassFlags::NeverCull).Write(output);
        unused = graph.AddPass("Drop", PassFlags::Raster).Write(unused);
        const CompileResult result = GraphCompiler::Compile(graph);
        const std::string dump = result.Dump();
        const std::string dot = result.DumpDot();
        Check(dump.find("cull:") != std::string::npos && dump.find("ZeroUseAllocation") != std::string::npos &&
                dump.find("pass=none") != std::string::npos && dump.find("resource=") != std::string::npos &&
                dump.find("\"Unused\"") != std::string::npos && dump.find("lifetime: skipped") != std::string::npos,
            "Zero-use dump keeps cull, names the resource, and skips lifetime");
        Check(dot.find("live") != std::string::npos && dot.find("culled") != std::string::npos,
            "DOT distinguishes live and culled passes");
    }

    return g_failures;
}
