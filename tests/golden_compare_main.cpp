#include "hdr_compare.h"
#include "image_compare.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>

namespace
{
    void PrintUsage()
    {
        std::printf(
            "Usage: RenderLabGoldenCompare [--mode gbuffer|hdr] --candidate <dir> --reference <dir> [--channel-swap]\n"
            "\n"
            "Default --mode is gbuffer (S1.6 PNG goldens).\n"
            "--mode hdr compares hdr-scene-color.rlhdr plus final.png (S3.2 tone-mapped LDR\n"
            "oracle) using docs/hdr-regression.md.\n"
            "Exit codes: 0 pass, 1 regression, 2 portability, 3 error.\n"
            "gbuffer --channel-swap: candidate roughness vs reference base-color (must fail).\n"
            "hdr --channel-swap: swap candidate R/B after decode and compare (must fail).\n");
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
    std::string mode = "gbuffer";

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
        if (EqualsOption(argv[index], "--mode"))
        {
            if (index + 1 >= argc)
            {
                std::fprintf(stderr, "error: --mode requires a value.\n");
                return 3;
            }
            mode = argv[++index];
            if (mode != "gbuffer" && mode != "hdr")
            {
                std::fprintf(stderr, "error: unknown mode '%s'.\n", mode.c_str());
                PrintUsage();
                return 3;
            }
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

    if (mode == "hdr")
    {
        using namespace renderlab::hdrgolden;

        if (channelSwap)
        {
            renderlab::RlHdrImage candidateFile;
            renderlab::RlHdrImage referenceFile;
            HdrRgbImage candidateRgb;
            HdrRgbImage referenceRgb;
            std::string error;
            const std::filesystem::path candidatePath = candidate / kHdrFileName;
            const std::filesystem::path referencePath = reference / kHdrFileName;
            if (!renderlab::LoadRlHdrFile(candidatePath, candidateFile, error) ||
                !renderlab::LoadRlHdrFile(referencePath, referenceFile, error) ||
                !DecodeRlHdrToRgb(candidateFile, candidateRgb, error) ||
                !DecodeRlHdrToRgb(referenceFile, referenceRgb, error))
            {
                std::fprintf(stderr, "error: %s\n", error.c_str());
                return 3;
            }

            for (size_t i = 0; i + 2 < candidateRgb.rgb.size(); i += 3)
            {
                std::swap(candidateRgb.rgb[i], candidateRgb.rgb[i + 2]);
            }

            const HdrCompareStats stats =
                CompareHdrRgb(candidateRgb, referenceRgb, kAbsTol, kRelTol, kRelativeFloor);
            const bool failed = !StatsPass(stats, kMaxMae, kMaxMismatchFraction);
            std::printf(
                "channel-swap: hdr R/B swap mae=%.4f mismatch=%.6f -> %s\n",
                stats.mae,
                stats.mismatchFraction,
                failed ? "failed as expected" : "UNEXPECTED PASS");
            return failed ? 0 : 1;
        }

        const HdrDirectoryCompareResult result = CompareHdrDirectories(candidate, reference);
        const std::string report = FormatHdrReport(result);
        std::fputs(report.c_str(), stdout);

        std::string reportError;
        const std::filesystem::path reportPath = candidate / kReportFileName;
        if (!WriteHdrReport(reportPath, result, reportError))
        {
            std::fprintf(stderr, "warning: %s\n", reportError.c_str());
        }
        else
        {
            std::printf("Wrote %s\n", reportPath.generic_string().c_str());
        }

        std::printf("verdict=%s\n", HdrVerdictName(result.verdict));
        return HdrVerdictExitCode(result.verdict);
    }

    using namespace renderlab::golden;

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
