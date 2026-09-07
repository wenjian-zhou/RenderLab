#include "hdr_compare.h"

#include <json/json.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

namespace renderlab::hdrgolden
{
    namespace
    {
        std::string ReadTextFile(const std::filesystem::path& path, std::string& error)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                error = "Failed to open '" + path.generic_string() + "'.";
                return {};
            }
            std::ostringstream buffer;
            buffer << input.rdbuf();
            return buffer.str();
        }

        bool ParseJsonObject(const std::string& text, Json::Value& root, std::string& error)
        {
            Json::CharReaderBuilder builder;
            builder["collectComments"] = false;
            builder["failIfExtra"] = true;
            builder["rejectDupKeys"] = true;
            const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
            std::string parseErrors;
            if (!reader->parse(text.data(), text.data() + text.size(), &root, &parseErrors) || !root.isObject())
            {
                error = "hdr-capture-metadata.json is not valid JSON";
                if (!parseErrors.empty())
                {
                    error += ": " + parseErrors;
                }
                else
                {
                    error += ".";
                }
                return false;
            }
            return true;
        }

        bool RequireJsonString(const Json::Value& root, const char* key, std::string& value, std::string& error)
        {
            if (!root.isMember(key) || !root[key].isString() || root[key].asString().empty())
            {
                error = std::string("hdr-capture-metadata.json is missing or empty '") + key + "'.";
                return false;
            }
            value = root[key].asString();
            return true;
        }

        bool RequireJsonUint(const Json::Value& root, const char* key, uint32_t& value, std::string& error)
        {
            if (!root.isMember(key))
            {
                error = std::string("hdr-capture-metadata.json is missing or invalid '") + key + "'.";
                return false;
            }

            const Json::Value& node = root[key];
            // jsoncpp isUInt()/isInt() are true for integral reals such as 1280.0.
            // Required metadata fields accept only JSON integer tokens.
            const Json::ValueType type = node.type();
            if (type == Json::uintValue)
            {
                if (!node.isUInt())
                {
                    error = std::string("hdr-capture-metadata.json is missing or invalid '") + key + "'.";
                    return false;
                }
                value = node.asUInt();
                return true;
            }
            if (type == Json::intValue)
            {
                if (!node.isInt() || node.asInt() < 0)
                {
                    error = std::string("hdr-capture-metadata.json is missing or invalid '") + key + "'.";
                    return false;
                }
                value = static_cast<uint32_t>(node.asInt());
                return true;
            }

            error = std::string("hdr-capture-metadata.json is missing or invalid '") + key + "'.";
            return false;
        }

        bool RequireJsonBool(const Json::Value& root, const char* key, bool& value, std::string& error)
        {
            if (!root.isMember(key) || !root[key].isBool())
            {
                error = std::string("hdr-capture-metadata.json is missing or invalid '") + key + "'.";
                return false;
            }
            value = root[key].asBool();
            return true;
        }

        bool RequireJsonFloat(const Json::Value& root, const char* key, float& value, std::string& error)
        {
            if (!root.isMember(key) || !root[key].isNumeric())
            {
                error = std::string("hdr-capture-metadata.json is missing or invalid '") + key + "'.";
                return false;
            }
            const double parsed = root[key].asDouble();
            if (!std::isfinite(parsed))
            {
                error = std::string("hdr-capture-metadata.json '") + key + "' is not finite.";
                return false;
            }
            value = static_cast<float>(parsed);
            return true;
        }

        std::string JsonEscape(std::string_view text)
        {
            std::string out;
            out.reserve(text.size());
            for (const char ch : text)
            {
                switch (ch)
                {
                case '\\':
                    out += "\\\\";
                    break;
                case '"':
                    out += "\\\"";
                    break;
                case '\n':
                    out += "\\n";
                    break;
                case '\r':
                    out += "\\r";
                    break;
                case '\t':
                    out += "\\t";
                    break;
                default:
                    out.push_back(ch);
                    break;
                }
            }
            return out;
        }
    }

    bool DecodeRlHdrToRgb(const RlHdrImage& image, HdrRgbImage& rgb, std::string& error)
    {
        error.clear();
        rgb = {};
        const uint64_t pixelCount = static_cast<uint64_t>(image.width) * image.height;
        if (pixelCount == 0 || image.rgba16.size() != static_cast<std::size_t>(pixelCount) * 4u)
        {
            error = "rlhdr RGB decode size mismatch";
            return false;
        }

        rgb.width = image.width;
        rgb.height = image.height;
        rgb.rgb.resize(static_cast<std::size_t>(pixelCount) * 3u);
        for (uint64_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const float r = HalfToFloat(image.rgba16[static_cast<std::size_t>(pixel) * 4u + 0u]);
            const float g = HalfToFloat(image.rgba16[static_cast<std::size_t>(pixel) * 4u + 1u]);
            const float b = HalfToFloat(image.rgba16[static_cast<std::size_t>(pixel) * 4u + 2u]);
            if (!std::isfinite(r) || !std::isfinite(g) || !std::isfinite(b))
            {
                error = "non-finite RGB";
                rgb = {};
                return false;
            }
            rgb.rgb[static_cast<std::size_t>(pixel) * 3u + 0u] = r;
            rgb.rgb[static_cast<std::size_t>(pixel) * 3u + 1u] = g;
            rgb.rgb[static_cast<std::size_t>(pixel) * 3u + 2u] = b;
        }
        return true;
    }

    bool HdrRgbHasNonFinite(const HdrRgbImage& image)
    {
        for (const float value : image.rgb)
        {
            if (!std::isfinite(value))
            {
                return true;
            }
        }
        return false;
    }

    HdrCompareStats CompareHdrRgb(
        const HdrRgbImage& candidate,
        const HdrRgbImage& reference,
        float absTol,
        float relTol,
        float relativeFloor)
    {
        HdrCompareStats stats;
        if (candidate.width != reference.width || candidate.height != reference.height ||
            candidate.rgb.size() != reference.rgb.size() ||
            candidate.rgb.size() != static_cast<std::size_t>(candidate.width) * candidate.height * 3u)
        {
            stats.dimensionMismatch = true;
            return stats;
        }

        const uint64_t pixelCount = static_cast<uint64_t>(candidate.width) * candidate.height;
        stats.pixelCount = pixelCount;
        if (pixelCount == 0)
        {
            stats.dimensionMismatch = true;
            return stats;
        }

        double absSum = 0.0;
        uint64_t mismatches = 0;
        for (uint64_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            bool pixelMismatch = false;
            for (uint32_t channel = 0; channel < 3u; ++channel)
            {
                const std::size_t index = static_cast<std::size_t>(pixel) * 3u + channel;
                const float cand = candidate.rgb[index];
                const float ref = reference.rgb[index];
                const double absErr = std::fabs(static_cast<double>(cand) - static_cast<double>(ref));
                const double relDen = std::max(std::fabs(static_cast<double>(ref)), static_cast<double>(relativeFloor));
                absSum += absErr;
                if (absErr > static_cast<double>(absTol) && absErr > static_cast<double>(relTol) * relDen)
                {
                    pixelMismatch = true;
                }
            }
            if (pixelMismatch)
            {
                ++mismatches;
            }
        }

        stats.mae = absSum / (static_cast<double>(pixelCount) * 3.0);
        stats.mismatchCount = mismatches;
        stats.mismatchFraction = static_cast<double>(mismatches) / static_cast<double>(pixelCount);
        return stats;
    }

    bool StatsPass(const HdrCompareStats& stats, double maxMae, double maxMismatchFraction)
    {
        return !stats.dimensionMismatch &&
            stats.mae <= maxMae &&
            stats.mismatchFraction <= maxMismatchFraction;
    }

    bool LoadHdrCaptureIdentity(
        const std::filesystem::path& directory,
        HdrCaptureIdentity& identity,
        std::string& error)
    {
        identity = {};
        const std::filesystem::path path = directory / kHdrMetadataFileName;
        if (!std::filesystem::exists(path))
        {
            error = "Missing " + std::string(kHdrMetadataFileName) + " under '" + directory.generic_string() + "'.";
            return false;
        }

        const std::string json = ReadTextFile(path, error);
        if (json.empty())
        {
            if (error.empty())
            {
                error = "hdr-capture-metadata.json under '" + directory.generic_string() + "' is empty.";
            }
            return false;
        }

        Json::Value root;
        if (!ParseJsonObject(json, root, error))
        {
            return false;
        }

        HdrCaptureIdentity loaded;
        if (!RequireJsonString(root, "schema", loaded.schema, error))
        {
            return false;
        }
        if (loaded.schema != kSchema)
        {
            error = "Schema mismatch: '" + loaded.schema + "' vs '" + kSchema + "'.";
            return false;
        }
        if (!RequireJsonString(root, "step", loaded.step, error) ||
            !RequireJsonString(root, "sceneId", loaded.sceneId, error) ||
            !RequireJsonString(root, "cameraPreset", loaded.cameraPreset, error) ||
            !RequireJsonString(root, "adapterName", loaded.adapterName, error) ||
            !RequireJsonString(root, "driverVersion", loaded.driverVersion, error) ||
            !RequireJsonString(root, "hdrFileName", loaded.hdrFileName, error) ||
            !RequireJsonString(root, "diagnosticFileName", loaded.diagnosticFileName, error) ||
            !RequireJsonString(root, "finalFileName", loaded.finalFileName, error) ||
            !RequireJsonUint(root, "width", loaded.width, error) ||
            !RequireJsonUint(root, "height", loaded.height, error) ||
            !RequireJsonUint(root, "frameIndex", loaded.frameIndex, error) ||
            !RequireJsonUint(root, "sampleCount", loaded.sampleCount, error) ||
            !RequireJsonUint(root, "nonFiniteCount", loaded.nonFiniteCount, error) ||
            !RequireJsonUint(root, "pixelCount", loaded.pixelCount, error) ||
            !RequireJsonFloat(root, "exposureEV", loaded.exposureEV, error) ||
            !RequireJsonBool(root, "verifyLights", loaded.verifyLights, error) ||
            !RequireJsonBool(root, "timestampValid", loaded.timestampValid, error))
        {
            return false;
        }
        if (loaded.width == 0 || loaded.height == 0)
        {
            error = "hdr-capture-metadata.json has invalid width/height.";
            return false;
        }
        if (static_cast<uint64_t>(loaded.pixelCount) !=
            static_cast<uint64_t>(loaded.width) * loaded.height)
        {
            error = "hdr-capture-metadata.json pixelCount does not match width*height.";
            return false;
        }

        loaded.hasMetadata = true;
        identity = std::move(loaded);
        return true;
    }

    bool IdentityMatchesLockedHdrCapture(const HdrCaptureIdentity& identity, std::string& error)
    {
        if (!identity.hasMetadata)
        {
            error = "HDR capture identity metadata is missing.";
            return false;
        }
        if (identity.schema != kSchema)
        {
            error = "Schema mismatch: '" + identity.schema + "' vs '" + kSchema + "'.";
            return false;
        }
        if (identity.sceneId != kSceneId)
        {
            error = "Scene mismatch: '" + identity.sceneId + "' vs '" + kSceneId + "'.";
            return false;
        }
        if (identity.cameraPreset != kCameraPreset)
        {
            error = "Camera preset mismatch: '" + identity.cameraPreset + "' vs '" + kCameraPreset + "'.";
            return false;
        }
        if (identity.width != kWidth || identity.height != kHeight)
        {
            error = "Resolution mismatch: expected 1280x720.";
            return false;
        }
        if (identity.frameIndex != kFrameIndex)
        {
            error = "Frame index mismatch: expected 1.";
            return false;
        }
        if (identity.sampleCount != kSampleCount)
        {
            error = "Sample count mismatch: expected 1.";
            return false;
        }
        if (identity.step != kStep)
        {
            error = "Step mismatch: '" + identity.step + "' vs '" + kStep + "'.";
            return false;
        }
        if (identity.hdrFileName != kHdrFileName)
        {
            error = "hdrFileName mismatch: '" + identity.hdrFileName + "' vs '" + kHdrFileName + "'.";
            return false;
        }
        if (identity.diagnosticFileName != kDiagnosticFileName)
        {
            error = "diagnosticFileName mismatch: '" + identity.diagnosticFileName + "' vs '" +
                kDiagnosticFileName + "'.";
            return false;
        }
        if (identity.finalFileName != kFinalFileName)
        {
            error = "finalFileName mismatch: '" + identity.finalFileName + "' vs '" +
                kFinalFileName + "'.";
            return false;
        }
        if (identity.exposureEV != kExposureEV)
        {
            char expected[32] = {};
            char actual[32] = {};
            std::snprintf(expected, sizeof(expected), "%g", static_cast<double>(kExposureEV));
            std::snprintf(actual, sizeof(actual), "%g", static_cast<double>(identity.exposureEV));
            error = std::string("Exposure mismatch: expected EV ") + expected + ", got " + actual +
                " (the locked capture pins exposureEV).";
            return false;
        }
        if (identity.verifyLights)
        {
            error = "verifyLights must be false for the locked HDR capture.";
            return false;
        }
        return true;
    }

    bool HdrIdentitiesCompatible(
        const HdrCaptureIdentity& candidate,
        const HdrCaptureIdentity& reference,
        std::string& error)
    {
        if (!IdentityMatchesLockedHdrCapture(reference, error))
        {
            error = "Reference " + error;
            return false;
        }
        if (!IdentityMatchesLockedHdrCapture(candidate, error))
        {
            error = "Candidate " + error;
            return false;
        }
        return true;
    }

    bool HdrAdaptersMatch(const HdrCaptureIdentity& candidate, const HdrCaptureIdentity& reference)
    {
        return candidate.hasMetadata && reference.hasMetadata &&
            !candidate.adapterName.empty() && !reference.adapterName.empty() &&
            candidate.adapterName == reference.adapterName &&
            candidate.driverVersion == reference.driverVersion;
    }

    HdrDirectoryCompareResult CompareHdrDirectories(
        const std::filesystem::path& candidateDir,
        const std::filesystem::path& referenceDir)
    {
        HdrDirectoryCompareResult result;
        std::string error;

        const bool loadedReference = LoadHdrCaptureIdentity(referenceDir, result.reference, error);
        if (!loadedReference)
        {
            result.verdict = Verdict::Error;
            result.summary = error;
            return result;
        }
        const bool loadedCandidate = LoadHdrCaptureIdentity(candidateDir, result.candidate, error);
        if (!loadedCandidate)
        {
            result.verdict = Verdict::Error;
            result.summary = error;
            return result;
        }

        std::string identityError;
        result.identityMatches = HdrIdentitiesCompatible(result.candidate, result.reference, identityError);
        result.adapterMatches = HdrAdaptersMatch(result.candidate, result.reference);

        if (result.reference.nonFiniteCount != 0 || result.candidate.nonFiniteCount != 0)
        {
            result.verdict = Verdict::Error;
            result.summary = "HDR metadata reports a non-zero nonFiniteCount.";
            return result;
        }

        RlHdrImage candidateDump;
        RlHdrImage referenceDump;
        if (!LoadRlHdrFile(referenceDir / kHdrFileName, referenceDump, error))
        {
            result.verdict = Verdict::Error;
            result.summary = error;
            return result;
        }
        if (!LoadRlHdrFile(candidateDir / kHdrFileName, candidateDump, error))
        {
            result.verdict = Verdict::Error;
            result.summary = error;
            return result;
        }

        if (candidateDump.width != kWidth || candidateDump.height != kHeight ||
            referenceDump.width != kWidth || referenceDump.height != kHeight)
        {
            result.verdict = Verdict::Error;
            result.summary = "HDR dump is not the locked 1280x720 golden resolution.";
            return result;
        }

        HdrRgbImage candidateRgb;
        HdrRgbImage referenceRgb;
        if (!DecodeRlHdrToRgb(referenceDump, referenceRgb, error))
        {
            result.verdict = Verdict::Error;
            result.summary = error;
            return result;
        }
        if (!DecodeRlHdrToRgb(candidateDump, candidateRgb, error))
        {
            result.verdict = Verdict::Error;
            result.summary = error;
            return result;
        }

        result.tight = CompareHdrRgb(candidateRgb, referenceRgb, kAbsTol, kRelTol, kRelativeFloor);
        result.portability = CompareHdrRgb(
            candidateRgb, referenceRgb, kPortabilityAbsTol, kPortabilityRelTol, kRelativeFloor);
        result.passedTight = StatsPass(result.tight, kMaxMae, kMaxMismatchFraction);
        result.passedPortability = StatsPass(
            result.portability, kPortabilityMaxMae, kPortabilityMaxMismatchFraction);

        golden::RgbImage referenceFinal;
        golden::RgbImage candidateFinal;
        if (!golden::LoadPngRgb(referenceDir / kFinalFileName, referenceFinal, error))
        {
            result.verdict = Verdict::Error;
            result.summary = error;
            return result;
        }
        if (!golden::LoadPngRgb(candidateDir / kFinalFileName, candidateFinal, error))
        {
            result.verdict = Verdict::Error;
            result.summary = error;
            return result;
        }

        if (candidateFinal.width != kWidth || candidateFinal.height != kHeight ||
            referenceFinal.width != kWidth || referenceFinal.height != kHeight)
        {
            result.verdict = Verdict::Error;
            result.summary = "final.png is not the locked 1280x720 golden resolution.";
            return result;
        }

        result.finalTight =
            golden::CompareRgb(candidateFinal, referenceFinal, kFinalPixelThreshold);
        result.finalPortability =
            golden::CompareRgb(candidateFinal, referenceFinal, kFinalPortabilityPixelThreshold);
        result.finalPassedTight =
            golden::StatsPass(result.finalTight, kFinalMaxMae, kFinalMaxMismatchFraction);
        result.finalPassedPortability = golden::StatsPass(
            result.finalPortability, kFinalPortabilityMaxMae, kFinalPortabilityMaxMismatchFraction);

        if (!result.identityMatches)
        {
            result.verdict = Verdict::Regression;
            result.summary = identityError.empty()
                ? "HDR capture identity does not match the locked scene/camera/resolution/frame."
                : identityError;
            return result;
        }
        if (result.passedTight && result.finalPassedTight)
        {
            result.verdict = Verdict::Pass;
            result.summary = result.adapterMatches
                ? "HDR dump and final.png are within the renderer-regression tolerances."
                : "HDR dump and final.png are within the renderer-regression tolerances on a "
                  "different adapter/driver.";
            return result;
        }
        if (!result.adapterMatches && result.passedPortability && result.finalPassedPortability)
        {
            result.verdict = Verdict::Portability;
            result.summary =
                "Adapter/driver differs from the approved golden environment and the HDR dump or "
                "final.png exceeds the tight renderer tolerances but both stay within the "
                "portability band. This is not classified as a renderer regression.";
            return result;
        }

        result.verdict = Verdict::Regression;
        result.summary = result.adapterMatches
            ? "HDR dump or final.png exceeds renderer-regression tolerances on the approved "
              "adapter/driver."
            : "HDR dump or final.png exceeds both renderer-regression and portability tolerances.";
        return result;
    }

    const char* HdrVerdictName(Verdict verdict)
    {
        switch (verdict)
        {
        case Verdict::Pass:
            return "pass";
        case Verdict::Regression:
            return "regression";
        case Verdict::Portability:
            return "portability";
        case Verdict::Error:
        default:
            return "error";
        }
    }

    int HdrVerdictExitCode(Verdict verdict)
    {
        switch (verdict)
        {
        case Verdict::Pass:
            return 0;
        case Verdict::Regression:
            return 1;
        case Verdict::Portability:
            return 2;
        case Verdict::Error:
        default:
            return 3;
        }
    }

    std::string FormatHdrReport(const HdrDirectoryCompareResult& result)
    {
        std::ostringstream out;
        out << "{\n";
        out << "  \"verdict\": \"" << HdrVerdictName(result.verdict) << "\",\n";
        out << "  \"summary\": \"" << JsonEscape(result.summary) << "\",\n";
        out << "  \"identityMatches\": " << (result.identityMatches ? "true" : "false") << ",\n";
        out << "  \"adapterMatches\": " << (result.adapterMatches ? "true" : "false") << ",\n";
        out << "  \"candidateAdapter\": \"" << JsonEscape(result.candidate.adapterName) << "\",\n";
        out << "  \"candidateDriver\": \"" << JsonEscape(result.candidate.driverVersion) << "\",\n";
        out << "  \"referenceAdapter\": \"" << JsonEscape(result.reference.adapterName) << "\",\n";
        out << "  \"referenceDriver\": \"" << JsonEscape(result.reference.driverVersion) << "\",\n";
        out << "  \"passedTight\": " << (result.passedTight ? "true" : "false") << ",\n";
        out << "  \"passedPortability\": " << (result.passedPortability ? "true" : "false") << ",\n";
        out << "  \"mae\": " << result.tight.mae << ",\n";
        out << "  \"mismatchCount\": " << result.tight.mismatchCount << ",\n";
        out << "  \"mismatchFraction\": " << result.tight.mismatchFraction << ",\n";
        out << "  \"finalPassedTight\": " << (result.finalPassedTight ? "true" : "false") << ",\n";
        out << "  \"finalPassedPortability\": "
            << (result.finalPassedPortability ? "true" : "false") << ",\n";
        out << "  \"finalMae\": " << result.finalTight.mae << ",\n";
        out << "  \"finalMismatchCount\": " << result.finalTight.mismatchCount << ",\n";
        out << "  \"finalMismatchFraction\": " << result.finalTight.mismatchFraction << ",\n";
        out << "  \"finalMaxAbs\": " << result.finalTight.maxAbs << "\n";
        out << "}\n";
        return out.str();
    }

    bool WriteHdrReport(
        const std::filesystem::path& path,
        const HdrDirectoryCompareResult& result,
        std::string& error)
    {
        std::error_code createError;
        std::filesystem::create_directories(path.parent_path(), createError);
        if (createError)
        {
            error = "Failed to create report directory: " + createError.message();
            return false;
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "Failed to write '" + path.generic_string() + "'.";
            return false;
        }
        output << FormatHdrReport(result);
        return true;
    }
}
