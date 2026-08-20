#include "image_compare.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

using namespace renderlab::golden;

namespace
{
    void PrintUsage()
    {
        std::printf(
            "Usage: RenderLabGoldenCompare --candidate <dir> --reference <dir> [--channel-swap]\n"
            "\n"
            "Compare an S1.6 GBuffer capture against the approved goldens.\n"
            "Exit codes: 0 pass, 1 regression, 2 portability, 3 error.\n"
            "--channel-swap compares the candidate roughness PNG against the reference\n"
            "base-color view and expects that comparison to fail.\n");
    }

    bool EqualsOption(const char* value, const char* option)
    {
        return std::strcmp(value, option) == 0;
    }
}

int main(int argc, char** argv)
{
    std::filesystem::path candidate;
    std::filesystem::path reference;
    bool channelSwap = false;

    for (int index = 1; index < argc; ++index)
    {
        if (EqualsOption(argv[index], "--help") || EqualsOption(argv[index], "-h"))
        {
            PrintUsage();
            return 0;
        }
        if (EqualsOption(argv[index], "--channel-swap"))
        {
            channelSwap = true;
            continue;
        }
        if (EqualsOption(argv[index], "--candidate"))
        {
            if (index + 1 >= argc)
            {
                std::fprintf(stderr, "error: --candidate requires a directory.\n");
                return 3;
            }
            candidate = argv[++index];
            continue;
        }
        if (EqualsOption(argv[index], "--reference"))
        {
            if (index + 1 >= argc)
            {
                std::fprintf(stderr, "error: --reference requires a directory.\n");
                return 3;
            }
            reference = argv[++index];
            continue;
        }

        std::fprintf(stderr, "error: unknown argument '%s'.\n", argv[index]);
        PrintUsage();
        return 3;
    }

    if (candidate.empty() || reference.empty())
    {
        std::fprintf(stderr, "error: --candidate and --reference are required.\n");
        PrintUsage();
        return 3;
    }

    if (channelSwap)
    {
        RgbImage candidateRoughness;
        RgbImage referenceBase;
        std::string error;
        const std::filesystem::path roughnessPath = candidate / GetViewRule(ViewId::Roughness).fileName;
        const std::filesystem::path basePath = reference / GetViewRule(ViewId::BaseColor).fileName;
        if (!LoadPngRgb(roughnessPath, candidateRoughness, error) ||
            !LoadPngRgb(basePath, referenceBase, error))
        {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 3;
        }

        const ViewRule& rule = GetViewRule(ViewId::BaseColor);
        const CompareStats stats = CompareRgb(candidateRoughness, referenceBase, rule.pixelThreshold);
        const bool failed = !StatsPass(stats, rule.maxMae, rule.maxMismatchFraction);
        std::printf(
            "channel-swap: roughness vs base-color mae=%.4f mismatch=%.6f maxAbs=%u -> %s\n",
            stats.mae,
            stats.mismatchFraction,
            stats.maxAbs,
            failed ? "failed as expected" : "UNEXPECTED PASS");
        return failed ? 0 : 1;
    }

    const DirectoryCompareResult result = CompareDirectories(candidate, reference);
    const std::string report = FormatReport(result);
    std::fputs(report.c_str(), stdout);

    std::string reportError;
    const std::filesystem::path reportPath = candidate / kReportFileName;
    if (!WriteReport(reportPath, result, reportError))
    {
        std::fprintf(stderr, "warning: %s\n", reportError.c_str());
    }
    else
    {
        std::printf("Wrote %s\n", reportPath.generic_string().c_str());
    }

    std::printf("verdict=%s\n", VerdictName(result.verdict));
    return VerdictExitCode(result.verdict);
}
