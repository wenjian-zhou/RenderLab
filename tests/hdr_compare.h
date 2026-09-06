#pragma once

#include "renderer/HdrDump.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace renderlab::hdrgolden
{
    inline constexpr uint32_t kWidth = 1280;
    inline constexpr uint32_t kHeight = 720;
    inline constexpr uint32_t kFrameIndex = 1;
    inline constexpr uint32_t kSampleCount = 1;
    inline constexpr const char* kSceneId = "cesium-milk-truck";
    inline constexpr const char* kCameraPreset = "s04-default";
    inline constexpr const char* kSchema = "renderlab-hdr-capture-metadata/v1";
    inline constexpr const char* kStep = "S2.4";
    inline constexpr const char* kHdrFileName = "hdr-scene-color.rlhdr";
    inline constexpr const char* kDiagnosticFileName = "lighting-lit.png";
    inline constexpr const char* kHdrMetadataFileName = "hdr-capture-metadata.json";
    inline constexpr const char* kReportFileName = "compare-report.json";
    inline constexpr const char* kRelativeDirectory = "cesium-milk-truck/s04-default/1280x720";
    inline constexpr float kRelativeFloor = 1e-4f;

    // Frozen from the first approved capture (bit-identical a-vs-b: mae 0, mismatch 0).
    inline constexpr float kAbsTol = 1e-4f;
    inline constexpr float kRelTol = 1e-3f;
    inline constexpr double kMaxMae = 1e-4;
    inline constexpr double kMaxMismatchFraction = 0.002;
    inline constexpr float kPortabilityAbsTol = 5e-4f;
    inline constexpr float kPortabilityRelTol = 5e-3f;
    inline constexpr double kPortabilityMaxMae = 1e-3;
    inline constexpr double kPortabilityMaxMismatchFraction = 0.02;

    struct HdrRgbImage
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<float> rgb;
    };

    struct HdrCompareStats
    {
        double mae = 0.0;
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

    struct HdrCaptureIdentity
    {
        std::string schema;
        std::string step;
        std::string sceneId;
        std::string cameraPreset;
        std::string adapterName;
        std::string driverVersion;
        std::string hdrFileName;
        std::string diagnosticFileName;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t frameIndex = 0;
        uint32_t sampleCount = 0;
        uint32_t nonFiniteCount = 0;
        uint32_t pixelCount = 0;
        bool verifyLights = true;
        bool hasMetadata = false;
        bool timestampValid = false;
    };

    struct HdrDirectoryCompareResult
    {
        Verdict verdict = Verdict::Error;
        bool adapterMatches = true;
        bool identityMatches = true;
        HdrCaptureIdentity reference;
        HdrCaptureIdentity candidate;
        HdrCompareStats tight;
        HdrCompareStats portability;
        bool passedTight = false;
        bool passedPortability = false;
        std::string summary;
    };

    bool DecodeRlHdrToRgb(const RlHdrImage& image, HdrRgbImage& rgb, std::string& error);
    bool HdrRgbHasNonFinite(const HdrRgbImage& image);
    HdrCompareStats CompareHdrRgb(
        const HdrRgbImage& candidate,
        const HdrRgbImage& reference,
        float absTol,
        float relTol,
        float relativeFloor);
    bool StatsPass(const HdrCompareStats& stats, double maxMae, double maxMismatchFraction);

    bool LoadHdrCaptureIdentity(
        const std::filesystem::path& directory,
        HdrCaptureIdentity& identity,
        std::string& error);
    bool IdentityMatchesLockedHdrCapture(const HdrCaptureIdentity& identity, std::string& error);
    bool HdrIdentitiesCompatible(
        const HdrCaptureIdentity& candidate,
        const HdrCaptureIdentity& reference,
        std::string& error);
    bool HdrAdaptersMatch(const HdrCaptureIdentity& candidate, const HdrCaptureIdentity& reference);

    HdrDirectoryCompareResult CompareHdrDirectories(
        const std::filesystem::path& candidateDir,
        const std::filesystem::path& referenceDir);

    std::string FormatHdrReport(const HdrDirectoryCompareResult& result);
    bool WriteHdrReport(
        const std::filesystem::path& path,
        const HdrDirectoryCompareResult& result,
        std::string& error);
    const char* HdrVerdictName(Verdict verdict);
    int HdrVerdictExitCode(Verdict verdict);
}
