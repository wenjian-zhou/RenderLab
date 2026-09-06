#include "hdr_compare.h"
#include "renderer/HdrDump.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace renderlab;
using namespace renderlab::hdrgolden;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* message)
    {
        if (condition)
        {
            std::printf("  PASS  %s\n", message);
            std::fflush(stdout);
            return;
        }
        std::printf("  FAIL  %s\n", message);
        std::fflush(stdout);
        ++g_failures;
    }

    bool WriteTextFile(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            return false;
        }
        output << text;
        return static_cast<bool>(output);
    }

    std::string MakeHdrIdentityJson(
        const char* step = kStep,
        const char* widthLiteral = "1280",
        const char* verifyLightsLiteral = "false",
        const char* suffix = "",
        const char* hdrFileName = kHdrFileName,
        const char* diagnosticFileName = kDiagnosticFileName,
        uint32_t nonFiniteCount = 0,
        uint32_t pixelCount = 921600)
    {
        std::ostringstream json;
        json << "{\n"
             << "  \"schema\": \"renderlab-hdr-capture-metadata/v1\",\n"
             << "  \"step\": \"" << step << "\",\n"
             << "  \"sceneId\": \"cesium-milk-truck\",\n"
             << "  \"cameraPreset\": \"s04-default\",\n"
             << "  \"width\": " << widthLiteral << ",\n"
             << "  \"height\": 720,\n"
             << "  \"frameIndex\": 1,\n"
             << "  \"sampleCount\": 1,\n"
             << "  \"verifyLights\": " << verifyLightsLiteral << ",\n"
             << "  \"adapterName\": \"Test Adapter\",\n"
             << "  \"driverVersion\": \"1.0\",\n"
             << "  \"hdrFileName\": \"" << hdrFileName << "\",\n"
             << "  \"diagnosticFileName\": \"" << diagnosticFileName << "\",\n"
             << "  \"nonFiniteCount\": " << nonFiniteCount << ",\n"
             << "  \"pixelCount\": " << pixelCount << ",\n"
             << "  \"timestampValid\": false\n"
             << "}\n"
             << suffix;
        return json.str();
    }

    void WriteHdrMetadata(const std::filesystem::path& directory, const std::string& json)
    {
        std::filesystem::create_directories(directory);
        WriteTextFile(directory / kHdrMetadataFileName, json);
    }

    std::filesystem::path FindHdrGoldenDirectory()
    {
#ifdef RENDERLAB_SOURCE_DIR
        const std::filesystem::path fromSource =
            std::filesystem::path(RENDERLAB_SOURCE_DIR) / "tests" / "golden-hdr" / kRelativeDirectory;
        if (std::filesystem::exists(fromSource / kHdrFileName))
        {
            return fromSource;
        }
#endif
        std::filesystem::path current = std::filesystem::current_path();
        for (int i = 0; i < 8; ++i)
        {
            const std::filesystem::path candidate =
                current / "tests" / "golden-hdr" / kRelativeDirectory;
            if (std::filesystem::exists(candidate / kHdrFileName))
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
}

int RunHdrCompareTests()
{
    std::printf("RenderLab S2.4 HDR dump / compare tests\n");
    std::fflush(stdout);

    Check(sizeof(RlHdrHeader) == 32, "RlHdrHeader is 32 bytes");
    Check(kRlHdrVersion == 1, "rlhdr version is 1");
    Check(kRlHdrFormatRgba16Float == 1, "format enum 1 is RGBA16_FLOAT");
    Check(std::memcmp(kRlHdrMagic, "RLHDR1", 6) == 0, "magic starts with RLHDR1");
    Check(kRlHdrMagic[6] == '\0' && kRlHdrMagic[7] == '\0', "magic trailing bytes are NUL");

    Check(HalfToFloat(FloatToHalf(0.f)) == 0.f, "half 0 round-trips");
    Check(std::fabs(HalfToFloat(FloatToHalf(1.f)) - 1.f) < 1e-3f, "half 1 round-trips");
    Check(std::fabs(HalfToFloat(FloatToHalf(4.f)) - 4.f) < 1e-2f, "half 4 round-trips");
    Check(!IsFiniteHalf(kHalfPositiveInf), "+Inf half is non-finite");
    Check(!IsFiniteHalf(kHalfNan), "NaN half is non-finite");
    Check(IsFiniteHalf(FloatToHalf(0.5f)), "0.5 half is finite");
    Check(HalfToFloat(0x0001) == 0x1p-24f, "min subnormal half is 2^-24");
    Check(FloatToHalf(HalfToFloat(0x0001)) == 0x0001, "min subnormal half round-trips");

    std::vector<uint16_t> pixels(4 * 4, 0);
    pixels[0] = FloatToHalf(0.25f);
    pixels[1] = FloatToHalf(0.5f);
    pixels[2] = FloatToHalf(1.f);
    pixels[3] = FloatToHalf(1.f);
    Check(CountNonFiniteRgb(pixels.data(), 2, 2) == 0, "finite 2x2 counts 0");
    pixels[4] = kHalfNan;
    Check(CountNonFiniteRgb(pixels.data(), 2, 2) == 1, "NaN in R counts 1 texel");
    pixels[4] = FloatToHalf(0.f);
    pixels[5] = kHalfPositiveInf;
    Check(CountNonFiniteRgb(pixels.data(), 2, 2) == 1, "Inf in G counts 1 texel");
    pixels[5] = FloatToHalf(0.f);
    pixels[7] = kHalfNan;
    Check(CountNonFiniteRgb(pixels.data(), 2, 2) == 0, "NaN in A is ignored");

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "renderlab-s24-roundtrip.rlhdr";
    std::string error;
    Check(WriteRlHdrFile(path, 2, 2, pixels.data(), error), "Write 2x2 rlhdr");
    RlHdrImage loaded;
    Check(LoadRlHdrFile(path, loaded, error), "Load 2x2 rlhdr");
    Check(loaded.width == 2 && loaded.height == 2, "Loaded size is 2x2");
    Check(loaded.rgba16.size() == 16, "Loaded 16 half values");
    Check(loaded.rgba16[0] == pixels[0] && loaded.rgba16[2] == pixels[2], "RGB halves round-trip");
    Check(std::filesystem::file_size(path) == 32 + 2 * 2 * 8, "File size is header plus tightly packed pixels");

    RlHdrImage bad;
    Check(!LoadRlHdrFile(path / "missing.rlhdr", bad, error), "Missing file fails");
    Check(!error.empty(), "Missing file sets error");

    const auto writeBytes = [](const std::filesystem::path& dest, const char* data, size_t size) {
        std::ofstream output(dest, std::ios::binary | std::ios::trunc);
        output.write(data, static_cast<std::streamsize>(size));
    };

    std::vector<char> raw(32 + 32);
    {
        std::ifstream input(path, std::ios::binary);
        input.read(raw.data(), static_cast<std::streamsize>(raw.size()));
    }

    const std::filesystem::path badMagicPath =
        std::filesystem::temp_directory_path() / "renderlab-s24-bad-magic.rlhdr";
    raw[0] = 'X';
    writeBytes(badMagicPath, raw.data(), raw.size());
    raw[0] = 'R';
    RlHdrImage badMagic;
    error.clear();
    Check(!LoadRlHdrFile(badMagicPath, badMagic, error), "Magic mismatch fails");
    Check(!error.empty(), "Magic mismatch sets error");

    const std::filesystem::path truncatedPath =
        std::filesystem::temp_directory_path() / "renderlab-s24-truncated.rlhdr";
    writeBytes(truncatedPath, raw.data(), raw.size() - 8);
    RlHdrImage truncated;
    error.clear();
    Check(!LoadRlHdrFile(truncatedPath, truncated, error), "Truncated 2x2 file fails");
    Check(!error.empty(), "Truncated file sets error");

    const std::filesystem::path oversizedPath =
        std::filesystem::temp_directory_path() / "renderlab-s24-oversized.rlhdr";
    std::vector<char> oversizedBytes = raw;
    oversizedBytes.insert(oversizedBytes.end(), 8, '\0');
    writeBytes(oversizedPath, oversizedBytes.data(), oversizedBytes.size());
    RlHdrImage oversized;
    error.clear();
    Check(!LoadRlHdrFile(oversizedPath, oversized, error), "Oversized 2x2 file fails");
    Check(!error.empty(), "Oversized file sets error");

    const std::filesystem::path reservedPath =
        std::filesystem::temp_directory_path() / "renderlab-s24-reserved.rlhdr";
    raw[24] = 1;
    writeBytes(reservedPath, raw.data(), raw.size());
    RlHdrImage reserved;
    error.clear();
    Check(!LoadRlHdrFile(reservedPath, reserved, error), "Non-zero reserved0 fails");
    Check(!error.empty(), "Non-zero reserved0 sets error");

    HdrRgbImage a;
    a.width = 2;
    a.height = 1;
    a.rgb = {0.f, 0.f, 0.f, 1.f, 2.f, 3.f};
    HdrRgbImage b = a;
    HdrCompareStats stats = CompareHdrRgb(a, b, 1e-3f, 1e-2f, 1e-4f);
    Check(stats.mismatchCount == 0 && stats.mae == 0.0, "Identical HDR images match");

    b.rgb[3] = 1.5f;
    stats = CompareHdrRgb(a, b, 1e-3f, 1e-2f, 1e-4f);
    Check(stats.mismatchCount == 1, "Channel that exceeds abs and rel mismatches");

    HdrRgbImage brightRef;
    brightRef.width = 1;
    brightRef.height = 1;
    brightRef.rgb = {10.f, 10.f, 10.f};
    HdrRgbImage brightCand = brightRef;
    brightCand.rgb[0] = 10.05f;
    stats = CompareHdrRgb(brightCand, brightRef, 1e-3f, 1e-2f, 1e-4f);
    Check(stats.mismatchCount == 0, "Small relative error on a bright pixel is tolerated");

    HdrRgbImage nanImg = a;
    nanImg.rgb[0] = std::numeric_limits<float>::quiet_NaN();
    Check(HdrRgbHasNonFinite(nanImg), "NaN RGB is non-finite");
    Check(!HdrRgbHasNonFinite(a), "finite RGB is finite");

    const std::filesystem::path metaDir =
        std::filesystem::temp_directory_path() / "renderlab-s24-meta";
    std::filesystem::create_directories(metaDir);
    {
        std::ofstream json(metaDir / kHdrMetadataFileName, std::ios::binary | std::ios::trunc);
        json << "{\n"
             << "  \"schema\": \"renderlab-hdr-capture-metadata/v1\",\n"
             << "  \"step\": \"S2.4\",\n"
             << "  \"sceneId\": \"cesium-milk-truck\",\n"
             << "  \"cameraPreset\": \"s04-default\",\n"
             << "  \"width\": 1280,\n"
             << "  \"height\": 720,\n"
             << "  \"frameIndex\": 1,\n"
             << "  \"sampleCount\": 1,\n"
             << "  \"verifyLights\": false,\n"
             << "  \"adapterName\": \"Test Adapter\",\n"
             << "  \"driverVersion\": \"1.0\",\n"
             << "  \"hdrFileName\": \"hdr-scene-color.rlhdr\",\n"
             << "  \"diagnosticFileName\": \"lighting-lit.png\",\n"
             << "  \"nonFiniteCount\": 0,\n"
             << "  \"pixelCount\": 921600,\n"
             << "  \"timestampValid\": false\n"
             << "}\n";
    }
    HdrCaptureIdentity identity;
    Check(LoadHdrCaptureIdentity(metaDir, identity, error), "Valid HDR metadata loads");
    Check(identity.step == kStep, "Valid HDR metadata stores step S2.4");
    Check(identity.hdrFileName == kHdrFileName, "Valid HDR metadata stores hdrFileName");
    Check(identity.diagnosticFileName == kDiagnosticFileName, "Valid HDR metadata stores diagnosticFileName");
    Check(identity.nonFiniteCount == 0, "Valid HDR metadata stores nonFiniteCount");
    Check(identity.pixelCount == 921600u, "Valid HDR metadata stores pixelCount");
    Check(IdentityMatchesLockedHdrCapture(identity, error), "Locked identity matches S2.4 constants");

    {
        std::ofstream json(metaDir / kHdrMetadataFileName, std::ios::binary | std::ios::trunc);
        json << "{ \"schema\": \"renderlab-capture-metadata/v1\", \"width\": 1280 }\n";
    }
    Check(!LoadHdrCaptureIdentity(metaDir, identity, error), "S1.6 schema is rejected");

    {
        std::ofstream json(metaDir / kHdrMetadataFileName, std::ios::binary | std::ios::trunc);
        json << "{\n"
             << "  \"schema\": \"renderlab-hdr-capture-metadata/v1\",\n"
             << "  \"step\": \"S2.4\",\n"
             << "  \"sceneId\": \"cesium-milk-truck\",\n"
             << "  \"cameraPreset\": \"s04-default\",\n"
             << "  \"width\": 1280.0,\n"
             << "  \"height\": 720,\n"
             << "  \"frameIndex\": 1,\n"
             << "  \"sampleCount\": 1,\n"
             << "  \"verifyLights\": false,\n"
             << "  \"adapterName\": \"Test Adapter\",\n"
             << "  \"driverVersion\": \"1.0\",\n"
             << "  \"hdrFileName\": \"hdr-scene-color.rlhdr\",\n"
             << "  \"diagnosticFileName\": \"lighting-lit.png\",\n"
             << "  \"nonFiniteCount\": 0,\n"
             << "  \"pixelCount\": 921600,\n"
             << "  \"timestampValid\": false\n"
             << "}\n";
    }
    Check(!LoadHdrCaptureIdentity(metaDir, identity, error), "Width as integral JSON real 1280.0 is an error");

    WriteHdrMetadata(metaDir, MakeHdrIdentityJson(kStep, "1280", "false", "oops\n"));
    Check(!LoadHdrCaptureIdentity(metaDir, identity, error),
          "Trailing garbage after a JSON object is an error");

    WriteHdrMetadata(metaDir, MakeHdrIdentityJson(kStep, "\"1280\""));
    Check(!LoadHdrCaptureIdentity(metaDir, identity, error), "Width as a JSON string is an error");

    WriteHdrMetadata(metaDir, MakeHdrIdentityJson());
    Check(LoadHdrCaptureIdentity(metaDir, identity, error), "Well-formed metadata reloads after strictness cases");
    WriteHdrMetadata(metaDir, MakeHdrIdentityJson(kStep, "1280", "true"));
    Check(LoadHdrCaptureIdentity(metaDir, identity, error), "verifyLights true still loads");
    Check(!IdentityMatchesLockedHdrCapture(identity, error),
          "verifyLights true is rejected against the locked capture");

    WriteHdrMetadata(metaDir, MakeHdrIdentityJson("S1.6"));
    Check(LoadHdrCaptureIdentity(metaDir, identity, error), "Wrong step still loads");
    Check(!IdentityMatchesLockedHdrCapture(identity, error), "Wrong step is rejected against the locked capture");

    WriteHdrMetadata(metaDir, MakeHdrIdentityJson(kStep, "1280", "false", "", "other.rlhdr"));
    Check(LoadHdrCaptureIdentity(metaDir, identity, error), "Wrong hdrFileName still loads");
    Check(!IdentityMatchesLockedHdrCapture(identity, error),
          "Wrong hdrFileName is rejected against the locked capture");

    WriteHdrMetadata(metaDir, MakeHdrIdentityJson(kStep, "1280", "false", "", kHdrFileName, "other.png"));
    Check(LoadHdrCaptureIdentity(metaDir, identity, error), "Wrong diagnosticFileName still loads");
    Check(!IdentityMatchesLockedHdrCapture(identity, error),
          "Wrong diagnosticFileName is rejected against the locked capture");

    WriteHdrMetadata(metaDir, MakeHdrIdentityJson(kStep, "1280", "false", "", kHdrFileName, kDiagnosticFileName, 0, 1));
    Check(!LoadHdrCaptureIdentity(metaDir, identity, error), "pixelCount not equal to width*height is an error");

    RlHdrImage finiteDump;
    finiteDump.width = 2;
    finiteDump.height = 2;
    finiteDump.rgba16.assign(16, FloatToHalf(0.f));
    finiteDump.rgba16[0] = FloatToHalf(0.25f);
    finiteDump.rgba16[1] = FloatToHalf(0.5f);
    finiteDump.rgba16[2] = FloatToHalf(1.f);
    finiteDump.rgba16[3] = FloatToHalf(1.f);
    HdrRgbImage decoded;
    error.clear();
    Check(DecodeRlHdrToRgb(finiteDump, decoded, error), "Finite 2x2 rlhdr decodes to RGB");
    Check(decoded.width == 2 && decoded.height == 2 && decoded.rgb.size() == 12, "Decoded RGB is 2x2");
    Check(std::fabs(decoded.rgb[0] - 0.25f) < 1e-3f && std::fabs(decoded.rgb[2] - 1.f) < 1e-3f,
          "Decoded RGB halves match");

    RlHdrImage nanDump = finiteDump;
    nanDump.rgba16[0] = kHalfNan;
    HdrRgbImage nanDecoded;
    error.clear();
    Check(!DecodeRlHdrToRgb(nanDump, nanDecoded, error), "NaN half in R fails decode");
    Check(error.find("non-finite RGB") != std::string::npos, "NaN decode error mentions non-finite RGB");

    HdrRgbImage swapped = decoded;
    std::swap(swapped.rgb[0], swapped.rgb[2]);
    stats = CompareHdrRgb(swapped, decoded, 1e-3f, 1e-2f, 1e-4f);
    Check(stats.mismatchCount > 0, "R/B swap mismatches CompareHdrRgb");

    const std::filesystem::path hdrDirs =
        std::filesystem::temp_directory_path() / "renderlab-s24-hdr-dirs";
    std::filesystem::remove_all(hdrDirs);
    const std::filesystem::path refDir = hdrDirs / "reference";
    WriteHdrMetadata(refDir, MakeHdrIdentityJson());
    std::vector<uint16_t> lockedZeros(static_cast<std::size_t>(kWidth) * kHeight * 4u, 0);
    Check(WriteRlHdrFile(refDir / kHdrFileName, kWidth, kHeight, lockedZeros.data(), error),
          "Write locked-size zero rlhdr");

    HdrDirectoryCompareResult dirResult = CompareHdrDirectories(refDir, refDir);
    Check(dirResult.verdict == Verdict::Pass, "Matching 1280x720 zero dumps pass");

    const std::filesystem::path missingJsonDir = hdrDirs / "missing-json";
    std::filesystem::create_directories(missingJsonDir);
    dirResult = CompareHdrDirectories(missingJsonDir, refDir);
    Check(dirResult.verdict == Verdict::Error, "Missing HDR metadata is an error");

    const std::filesystem::path missingDumpDir = hdrDirs / "missing-dump";
    WriteHdrMetadata(missingDumpDir, MakeHdrIdentityJson());
    dirResult = CompareHdrDirectories(missingDumpDir, refDir);
    Check(dirResult.verdict == Verdict::Error, "Valid metadata with missing rlhdr is an error");

    const std::filesystem::path smallDumpDir = hdrDirs / "small-dump";
    WriteHdrMetadata(smallDumpDir, MakeHdrIdentityJson());
    Check(WriteRlHdrFile(smallDumpDir / kHdrFileName, 2, 2, finiteDump.rgba16.data(), error),
          "Write 2x2 candidate dump");
    dirResult = CompareHdrDirectories(smallDumpDir, refDir);
    Check(dirResult.verdict == Verdict::Error, "2x2 HDR dump is not the locked resolution");

    const std::filesystem::path verifyDir = hdrDirs / "verify-lights";
    WriteHdrMetadata(verifyDir, MakeHdrIdentityJson(kStep, "1280", "true"));
    Check(WriteRlHdrFile(verifyDir / kHdrFileName, kWidth, kHeight, lockedZeros.data(), error),
          "Write verifyLights candidate dump");
    dirResult = CompareHdrDirectories(verifyDir, refDir);
    Check(dirResult.verdict == Verdict::Regression, "verifyLights true directory compare is a regression");

    const std::filesystem::path wrongStepDir = hdrDirs / "wrong-step";
    WriteHdrMetadata(wrongStepDir, MakeHdrIdentityJson("S1.6"));
    Check(WriteRlHdrFile(wrongStepDir / kHdrFileName, kWidth, kHeight, lockedZeros.data(), error),
          "Write wrong-step candidate dump");
    dirResult = CompareHdrDirectories(wrongStepDir, refDir);
    Check(dirResult.verdict == Verdict::Regression, "Wrong step directory compare is a regression");

    WriteHdrMetadata(metaDir, MakeHdrIdentityJson(kStep, "1280", "false", "", kHdrFileName,
                                                 kDiagnosticFileName, 1));
    Check(LoadHdrCaptureIdentity(metaDir, identity, error), "nonFiniteCount 1 still loads");
    Check(identity.nonFiniteCount == 1, "nonFiniteCount 1 is stored");

    const std::filesystem::path nonFiniteMetaDir = hdrDirs / "nonfinite-meta";
    WriteHdrMetadata(nonFiniteMetaDir, MakeHdrIdentityJson(kStep, "1280", "false", "", kHdrFileName,
                                                          kDiagnosticFileName, 1));
    Check(WriteRlHdrFile(nonFiniteMetaDir / kHdrFileName, kWidth, kHeight, lockedZeros.data(), error),
          "Write nonFiniteCount candidate dump");
    dirResult = CompareHdrDirectories(nonFiniteMetaDir, refDir);
    Check(dirResult.verdict == Verdict::Error, "nonFiniteCount != 0 is a directory compare error");

    const std::filesystem::path goldenDir = FindHdrGoldenDirectory();
    if (goldenDir.empty())
    {
        std::printf("  SKIP  Committed HDR goldens are not present under tests/golden-hdr\n");
        std::fflush(stdout);
    }
    else
    {
        const HdrDirectoryCompareResult self = CompareHdrDirectories(goldenDir, goldenDir);
        Check(self.verdict == Verdict::Pass, "Comparing approved HDR goldens to themselves passes");
        Check(self.adapterMatches, "HDR self-compare records matching adapter metadata");
        Check(self.identityMatches, "HDR self-compare records matching scene/camera/resolution/frame");
    }

    return g_failures;
}
