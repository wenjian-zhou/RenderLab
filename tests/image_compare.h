#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace renderlab::golden
{
    inline constexpr uint32_t kWidth = 1280;
    inline constexpr uint32_t kHeight = 720;
    inline constexpr uint32_t kFrameIndex = 1;
    inline constexpr uint32_t kViewCount = 6;
    inline constexpr const char* kSceneId = "cesium-milk-truck";
    inline constexpr const char* kCameraPreset = "s04-default";
    inline constexpr const char* kMetadataFileName = "capture-metadata.json";
    inline constexpr const char* kReportFileName = "compare-report.json";
    inline constexpr const char* kRelativeDirectory = "cesium-milk-truck/s04-default/1280x720";
    inline constexpr const char* kSchema = "renderlab-capture-metadata/v1";

    enum class ViewId : uint32_t
    {
        BaseColor = 0,
        WorldNormal = 1,
        Roughness = 2,
        Metallic = 3,
        AoFlags = 4,
        LinearDepth = 5,
        Count = 6
    };

    struct ViewRule
    {
        ViewId id = ViewId::BaseColor;
        const char* cliName = "";
        const char* fileName = "";
        const char* displayName = "";
        uint32_t pixelThreshold = 2;
        double maxMae = 1.0;
        double maxMismatchFraction = 0.002;
        uint32_t portabilityPixelThreshold = 8;
        double portabilityMaxMae = 8.0;
        double portabilityMaxMismatchFraction = 0.05;
        bool weakOracle = false;
        const char* notes = "";
    };

    struct RgbImage
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> rgb;
    };

    struct CompareStats
    {
        double maeR = 0.0;
        double maeG = 0.0;
        double maeB = 0.0;
        double mae = 0.0;
        double rmse = 0.0;
        uint32_t maxAbs = 0;
        uint64_t mismatchCount = 0;
        uint64_t pixelCount = 0;
        double mismatchFraction = 0.0;
        bool dimensionMismatch = false;
    };

    enum class Verdict
    {
        Pass,
        Regression,
        Portability,
        Error
    };

    struct CaptureIdentity
    {
        std::string sceneId;
        std::string cameraPreset;
        std::string adapterName;
        std::string driverVersion;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t frameIndex = 0;
        bool hasMetadata = false;
    };

    struct ViewCompareResult
    {
        ViewRule rule;
        CompareStats tight;
        CompareStats portability;
        bool passedTight = false;
        bool passedPortability = false;
        std::string message;
    };

    struct DirectoryCompareResult
    {
        Verdict verdict = Verdict::Error;
        bool adapterMatches = true;
        bool identityMatches = true;
        CaptureIdentity reference;
        CaptureIdentity candidate;
        std::vector<ViewCompareResult> views;
        std::string summary;
    };

    const ViewRule& GetViewRule(ViewId id);
    const ViewRule* FindViewRuleByFileName(std::string_view fileName);

    bool LoadPngRgb(const std::filesystem::path& path, RgbImage& image, std::string& error);
    CompareStats CompareRgb(const RgbImage& candidate, const RgbImage& reference, uint32_t pixelThreshold);
    bool StatsPass(const CompareStats& stats, double maxMae, double maxMismatchFraction);

    bool LoadCaptureIdentity(const std::filesystem::path& directory, CaptureIdentity& identity, std::string& error);
    bool IdentitiesCompatible(const CaptureIdentity& candidate, const CaptureIdentity& reference, std::string& error);
    bool AdaptersMatch(const CaptureIdentity& candidate, const CaptureIdentity& reference);

    DirectoryCompareResult CompareDirectories(
        const std::filesystem::path& candidateDir,
        const std::filesystem::path& referenceDir);

    std::string FormatReport(const DirectoryCompareResult& result);
    bool WriteReport(const std::filesystem::path& path, const DirectoryCompareResult& result, std::string& error);
    const char* VerdictName(Verdict verdict);
    int VerdictExitCode(Verdict verdict);
}
