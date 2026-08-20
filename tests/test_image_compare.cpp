#include "image_compare.h"
#include "renderer/GBufferDebugPass.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

using namespace renderlab;
using namespace renderlab::golden;

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

    RgbImage MakeSolid(uint32_t width, uint32_t height, uint8_t r, uint8_t g, uint8_t b)
    {
        RgbImage image;
        image.width = width;
        image.height = height;
        image.rgb.assign(static_cast<std::size_t>(width) * height * 3u, 0);
        for (uint32_t i = 0; i < width * height; ++i)
        {
            image.rgb[i * 3u + 0] = r;
            image.rgb[i * 3u + 1] = g;
            image.rgb[i * 3u + 2] = b;
        }
        return image;
    }

    RgbImage WithChannelSwapRB(const RgbImage& source)
    {
        RgbImage image = source;
        for (std::size_t i = 0; i + 2 < image.rgb.size(); i += 3)
        {
            std::swap(image.rgb[i], image.rgb[i + 2]);
        }
        return image;
    }

    std::filesystem::path FindGoldenDirectory()
    {
#ifdef RENDERLAB_SOURCE_DIR
        const std::filesystem::path fromSource =
            std::filesystem::path(RENDERLAB_SOURCE_DIR) / "tests" / "golden" / kRelativeDirectory;
        if (std::filesystem::exists(fromSource / GetViewRule(ViewId::BaseColor).fileName))
        {
            return fromSource;
        }
#endif
        std::filesystem::path current = std::filesystem::current_path();
        for (int i = 0; i < 8; ++i)
        {
            const std::filesystem::path candidate =
                current / "tests" / "golden" / kRelativeDirectory;
            if (std::filesystem::exists(candidate / GetViewRule(ViewId::BaseColor).fileName))
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

    std::string MakeIdentityJson(
        const char* sceneId = kSceneId,
        const char* widthLiteral = "1280",
        const char* suffix = "")
    {
        std::ostringstream json;
        json << "{\n"
             << "  \"schema\": \"renderlab-capture-metadata/v1\",\n"
             << "  \"sceneId\": \"" << sceneId << "\",\n"
             << "  \"cameraPreset\": \"s04-default\",\n"
             << "  \"adapterName\": \"NVIDIA GeForce RTX 4070 SUPER\",\n"
             << "  \"driverVersion\": \"32.0.15.7688\",\n"
             << "  \"width\": " << widthLiteral << ",\n"
             << "  \"height\": 720,\n"
             << "  \"frameIndex\": 1,\n"
             << "  \"sampleCount\": 1\n"
             << "}\n"
             << suffix;
        return json.str();
    }

    std::filesystem::path MakeTempMetadataDir(const std::string& json)
    {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "renderlab-s16-identity-meta";
        std::filesystem::create_directories(dir);
        WriteTextFile(dir / kMetadataFileName, json);
        return dir;
    }

    std::filesystem::path MakeTempCaptureDir(const std::filesystem::path& goldenDir, const std::string& json)
    {
        const std::filesystem::path dir =
            std::filesystem::temp_directory_path() / "renderlab-s16-identity-capture";
        std::error_code ignored;
        std::filesystem::remove_all(dir, ignored);
        std::filesystem::create_directories(dir);
        for (uint32_t index = 0; index < kViewCount; ++index)
        {
            const char* fileName = GetViewRule(static_cast<ViewId>(index)).fileName;
            std::filesystem::copy_file(goldenDir / fileName, dir / fileName);
        }
        WriteTextFile(dir / kMetadataFileName, json);
        return dir;
    }
}

int RunImageCompareTests()
{
    std::printf("RenderLab S1.6 image-regression comparison tests\n");

    Check(kViewCount == 6u, "Golden harness covers the six ADR-002 views");
    Check(kWidth == 1280u && kHeight == 720u, "Locked golden resolution is 1280x720");
    Check(kFrameIndex == 1u, "Locked golden frame index is 1");
    Check(kSampleCount == 1u, "Locked golden sample count is 1");
    Check(std::string(kSceneId) == "cesium-milk-truck", "Locked golden scene is cesium-milk-truck");
    Check(std::string(kCameraPreset) == "s04-default", "Locked golden camera is s04-default");
    Check(std::string(kSchema) == "renderlab-capture-metadata/v1", "Locked golden schema is v1");

    for (uint32_t index = 0; index < kViewCount; ++index)
    {
        const ViewId id = static_cast<ViewId>(index);
        const ViewRule& rule = GetViewRule(id);
        const GBufferDebugModeInfo& info = GetGBufferDebugModeInfo(static_cast<GBufferDebugMode>(index));
        Check(std::string(rule.fileName) == info.dumpFileName, "Golden file names reuse S1.5 dump names");
        Check(std::string(rule.cliName) == info.cliName, "Golden CLI names match ADR-002 debug modes");
        Check(rule.pixelThreshold > 0, "Tight comparison is not exact byte equality");
        Check(rule.portabilityPixelThreshold >= rule.pixelThreshold, "Portability band is not tighter than regression");
    }

    Check(GetViewRule(ViewId::Metallic).weakOracle, "Milk Truck metallic is a weak oracle");
    Check(!GetViewRule(ViewId::BaseColor).weakOracle, "Base color is a full oracle");
    Check(GetViewRule(ViewId::LinearDepth).maxMae > GetViewRule(ViewId::BaseColor).maxMae,
          "Linearized depth has a looser MAE than base color");

    const RgbImage red = MakeSolid(8, 8, 200, 16, 16);
    const RgbImage redCopy = red;
    const CompareStats identical = CompareRgb(red, redCopy, 2);
    Check(!identical.dimensionMismatch, "Identical images have matching dimensions");
    Check(identical.mae == 0.0 && identical.maxAbs == 0 && identical.mismatchCount == 0,
          "Identical images have zero MAE and maxAbs");
    Check(StatsPass(identical, 1.0, 0.002), "Identical images pass the tight rule");

    RgbImage slightlyOff = red;
    slightlyOff.rgb[0] = static_cast<uint8_t>(slightlyOff.rgb[0] - 1);
    const CompareStats tiny = CompareRgb(slightlyOff, red, 2);
    Check(tiny.maxAbs == 1 && tiny.mismatchCount == 0, "A 1-count channel delta is inside pixelThreshold 2");
    Check(StatsPass(tiny, 1.0, 0.002), "Sub-threshold noise is not a regression");

    const RgbImage swappedRB = WithChannelSwapRB(red);
    const CompareStats swapped = CompareRgb(swappedRB, red, 2);
    Check(swapped.mismatchFraction == 1.0, "An R/B channel swap mismatches every pixel");
    Check(!StatsPass(swapped, 1.0, 0.002), "A deliberate channel swap fails the tight rule");

    const RgbImage sizedWrong = MakeSolid(4, 4, 200, 16, 16);
    const CompareStats dim = CompareRgb(sizedWrong, red, 2);
    Check(dim.dimensionMismatch, "Dimension mismatch is reported");
    Check(!StatsPass(dim, 1.0, 0.002), "Dimension mismatch does not pass");

    Check(VerdictExitCode(Verdict::Pass) == 0, "Pass exit code is 0");
    Check(VerdictExitCode(Verdict::Regression) == 1, "Regression exit code is 1");
    Check(VerdictExitCode(Verdict::Portability) == 2, "Portability exit code is 2");
    Check(std::string(VerdictName(Verdict::Portability)) == "portability", "Portability is a distinct verdict");

    const std::filesystem::path goldenDir = FindGoldenDirectory();
    if (goldenDir.empty())
    {
        Check(false, "Committed goldens are present under tests/golden/cesium-milk-truck/s04-default/1280x720");
        return g_failures;
    }

    Check(std::filesystem::exists(goldenDir / kMetadataFileName), "Approved capture-metadata.json is committed");

    const DirectoryCompareResult self = CompareDirectories(goldenDir, goldenDir);
    Check(self.verdict == Verdict::Pass, "Comparing approved goldens to themselves passes");
    Check(self.adapterMatches, "Self-compare records matching adapter metadata");
    Check(self.identityMatches, "Self-compare records matching scene/camera/resolution/frame");

    RgbImage baseColor;
    RgbImage roughness;
    RgbImage metallic;
    std::string loadError;
    Check(LoadPngRgb(goldenDir / GetViewRule(ViewId::BaseColor).fileName, baseColor, loadError),
          "Load committed base-color golden");
    Check(LoadPngRgb(goldenDir / GetViewRule(ViewId::Roughness).fileName, roughness, loadError),
          "Load committed roughness golden");
    Check(LoadPngRgb(goldenDir / GetViewRule(ViewId::Metallic).fileName, metallic, loadError),
          "Load committed metallic golden");
    Check(baseColor.width == kWidth && baseColor.height == kHeight, "Committed goldens are 1280x720");

    const CompareStats swappedViews = CompareRgb(roughness, baseColor, GetViewRule(ViewId::BaseColor).pixelThreshold);
    Check(!StatsPass(swappedViews, GetViewRule(ViewId::BaseColor).maxMae, GetViewRule(ViewId::BaseColor).maxMismatchFraction),
          "Using roughness as base-color fails (channel-swap proof)");

    const CompareStats metallicAsBase = CompareRgb(metallic, baseColor, GetViewRule(ViewId::BaseColor).pixelThreshold);
    Check(!StatsPass(metallicAsBase, GetViewRule(ViewId::BaseColor).maxMae, GetViewRule(ViewId::BaseColor).maxMismatchFraction),
          "Using metallic as base-color fails");

    double metallicMean = 0.0;
    if (!metallic.rgb.empty())
    {
        double sum = 0.0;
        for (uint8_t value : metallic.rgb)
        {
            sum += value;
        }
        metallicMean = sum / static_cast<double>(metallic.rgb.size());
    }
    Check(metallicMean < 16.0, "Milk Truck metallic golden stays near-black (weak oracle)");

    CaptureIdentity identity;
    std::string identityError;
    Check(LoadCaptureIdentity(goldenDir, identity, identityError), "Load approved capture metadata");
    Check(identity.schema == kSchema, "Approved metadata schema is v1");
    Check(identity.sceneId == kSceneId, "Approved metadata scene is cesium-milk-truck");
    Check(identity.cameraPreset == kCameraPreset, "Approved metadata camera is s04-default");
    Check(identity.width == kWidth && identity.height == kHeight, "Approved metadata resolution is 1280x720");
    Check(identity.frameIndex == kFrameIndex, "Approved metadata frame is 1");
    Check(identity.sampleCount == kSampleCount, "Approved metadata sample count is 1");
    Check(!identity.adapterName.empty() && !identity.driverVersion.empty(),
          "Approved metadata records adapter and driver");
    Check(IdentityMatchesLockedCapture(identity, identityError), "Approved metadata matches the locked capture");

    CaptureIdentity emptyIdentity;
    Check(!IdentityMatchesLockedCapture(emptyIdentity, identityError),
          "Empty identity does not match the locked capture");
    Check(!IdentitiesCompatible(emptyIdentity, identity, identityError),
          "Empty candidate identity is incompatible with the approved golden");

    const std::filesystem::path emptyMetaDir = MakeTempMetadataDir("{}\n");
    CaptureIdentity emptyLoaded;
    Check(!LoadCaptureIdentity(emptyMetaDir, emptyLoaded, identityError),
          "Empty metadata object fails to load");
    Check(!emptyLoaded.hasMetadata, "Failed load does not report hasMetadata");

    const std::filesystem::path missingFieldsDir = MakeTempMetadataDir(
        "{\n  \"adapterName\": \"NVIDIA GeForce RTX 4070 SUPER\",\n  \"driverVersion\": \"32.0.15.7688\"\n}\n");
    CaptureIdentity missingFields;
    Check(!LoadCaptureIdentity(missingFieldsDir, missingFields, identityError),
          "Metadata missing scene/camera/schema/frame fails to load");

    CaptureIdentity malformedNumber;
    Check(!LoadCaptureIdentity(MakeTempMetadataDir(MakeIdentityJson(kSceneId, "1280oops")), malformedNumber, identityError),
          "Malformed number 1280oops is not accepted as 1280");
    Check(!LoadCaptureIdentity(MakeTempMetadataDir(MakeIdentityJson(kSceneId, "1280", "oops\n")), malformedNumber, identityError),
          "Trailing garbage after a JSON object is an error");
    Check(!LoadCaptureIdentity(MakeTempMetadataDir(MakeIdentityJson(kSceneId, "\"1280\"")), malformedNumber, identityError),
          "Width as a JSON string is an error");
    Check(!LoadCaptureIdentity(MakeTempMetadataDir(MakeIdentityJson(kSceneId, "1280.5")), malformedNumber, identityError),
          "Width as a JSON real is an error");
    Check(!LoadCaptureIdentity(MakeTempMetadataDir("{ \"schema\": "), malformedNumber, identityError),
          "Truncated JSON document is an error");

    const std::filesystem::path wrongSceneDir = MakeTempMetadataDir(MakeIdentityJson("fallback-boxes"));
    CaptureIdentity wrongScene;
    Check(LoadCaptureIdentity(wrongSceneDir, wrongScene, identityError),
          "Well-formed metadata with the wrong scene still loads");
    Check(!IdentityMatchesLockedCapture(wrongScene, identityError),
          "Wrong scene is rejected against the locked capture");
    Check(!IdentitiesCompatible(wrongScene, identity, identityError),
          "Wrong scene is incompatible with the approved golden");

    const std::filesystem::path emptyCaptureDir = MakeTempCaptureDir(goldenDir, "{}\n");
    const DirectoryCompareResult emptyCompare = CompareDirectories(emptyCaptureDir, goldenDir);
    Check(emptyCompare.verdict == Verdict::Error,
          "Matching PNGs with empty metadata are an error, not a pass");
    std::filesystem::remove_all(emptyCaptureDir);

    const std::filesystem::path wrongSceneCaptureDir =
        MakeTempCaptureDir(goldenDir, MakeIdentityJson("fallback-boxes"));
    const DirectoryCompareResult wrongSceneCompare = CompareDirectories(wrongSceneCaptureDir, goldenDir);
    Check(wrongSceneCompare.verdict == Verdict::Regression,
          "Well-formed metadata with the wrong scene is a regression");
    std::filesystem::remove_all(wrongSceneCaptureDir);

    return g_failures;
}
