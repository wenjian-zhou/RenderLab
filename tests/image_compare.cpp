#include "image_compare.h"

#include <json/json.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <utility>

namespace renderlab::golden
{
    namespace
    {
        constexpr ViewRule kRules[] = {
            {
                ViewId::BaseColor,
                "base-color",
                "gbuffer-base-color.png",
                "Base color",
                2, 1.0, 0.002,
                8, 8.0, 0.05,
                false,
                "8-bit display-referred sRGB dump of linear GBufferA.rgb. "
                "Not lighting and not the S3 tone map. Avoid exact PNG byte equality."
            },
            {
                ViewId::WorldNormal,
                "world-normal",
                "gbuffer-world-normal.png",
                "World normal",
                3, 1.5, 0.005,
                10, 10.0, 0.08,
                false,
                "Display remap 0.5 * n + 0.5 of raw float16 world normals. "
                "Background stays black. Quantization and GPU differences are expected."
            },
            {
                ViewId::Roughness,
                "roughness",
                "gbuffer-roughness.png",
                "Roughness",
                2, 1.0, 0.002,
                8, 8.0, 0.05,
                false,
                "Perceptual roughness as grayscale. Milk Truck roughness is 1 on most surfaces."
            },
            {
                ViewId::Metallic,
                "metallic",
                "gbuffer-metallic.png",
                "Metallic",
                2, 1.0, 0.002,
                8, 8.0, 0.05,
                true,
                "WEAK ORACLE: Cesium Milk Truck metallic is 0, so this view is near-black. "
                "It catches a channel swap onto a non-black view, not a metallic material regression. "
                "The S0.4 camera is not changed to frame GoldMetal."
            },
            {
                ViewId::AoFlags,
                "ao-flags",
                "gbuffer-ao-flags.png",
                "AO / material flags",
                2, 1.0, 0.002,
                8, 8.0, 0.05,
                false,
                "R=AO, G=ShadingValid, B=TwoSided. Background is black."
            },
            {
                ViewId::LinearDepth,
                "linear-depth",
                "gbuffer-linear-depth.png",
                "Linearized depth",
                4, 2.0, 0.010,
                12, 12.0, 0.10,
                false,
                "8-bit dump of viewZ/(viewZ+1) from reversed-Z float depth. "
                "Background is magenta and must not be reconstructed. "
                "Exact byte equality is especially inappropriate here."
            },
        };

        static_assert(sizeof(kRules) / sizeof(kRules[0]) == kViewCount);

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
                error = "capture-metadata.json is not valid JSON";
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
                error = std::string("capture-metadata.json is missing or empty '") + key + "'.";
                return false;
            }
            value = root[key].asString();
            return true;
        }

        bool RequireJsonUint(const Json::Value& root, const char* key, uint32_t& value, std::string& error)
        {
            if (!root.isMember(key))
            {
                error = std::string("capture-metadata.json is missing or invalid '") + key + "'.";
                return false;
            }

            const Json::Value& node = root[key];
            if (node.type() == Json::uintValue)
            {
                value = node.asUInt();
                return true;
            }
            if (node.type() == Json::intValue && node.asInt() >= 0)
            {
                value = static_cast<uint32_t>(node.asInt());
                return true;
            }

            error = std::string("capture-metadata.json is missing or invalid '") + key + "'.";
            return false;
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

    const ViewRule& GetViewRule(ViewId id)
    {
        const uint32_t index = static_cast<uint32_t>(id);
        if (index >= kViewCount)
        {
            return kRules[0];
        }
        return kRules[index];
    }

    const ViewRule* FindViewRuleByFileName(std::string_view fileName)
    {
        for (const ViewRule& rule : kRules)
        {
            if (fileName == rule.fileName)
            {
                return &rule;
            }
        }
        return nullptr;
    }

    bool LoadPngRgb(const std::filesystem::path& path, RgbImage& image, std::string& error)
    {
        image = {};
        int width = 0;
        int height = 0;
        int components = 0;
        const std::string pathString = path.string();
        stbi_uc* data = stbi_load(pathString.c_str(), &width, &height, &components, 3);
        if (!data)
        {
            error = "Failed to load PNG '" + path.generic_string() + "': " +
                std::string(stbi_failure_reason() ? stbi_failure_reason() : "unknown");
            return false;
        }
        if (width <= 0 || height <= 0)
        {
            stbi_image_free(data);
            error = "PNG '" + path.generic_string() + "' has invalid dimensions.";
            return false;
        }

        image.width = static_cast<uint32_t>(width);
        image.height = static_cast<uint32_t>(height);
        image.rgb.assign(data, data + (static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u));
        stbi_image_free(data);
        return true;
    }

    CompareStats CompareRgb(const RgbImage& candidate, const RgbImage& reference, uint32_t pixelThreshold)
    {
        CompareStats stats;
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

        double sumR = 0.0;
        double sumG = 0.0;
        double sumB = 0.0;
        double sumSq = 0.0;
        uint32_t maxAbs = 0;
        uint64_t mismatches = 0;

        for (uint64_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const uint8_t* cand = candidate.rgb.data() + pixel * 3u;
            const uint8_t* ref = reference.rgb.data() + pixel * 3u;
            const int dR = static_cast<int>(cand[0]) - static_cast<int>(ref[0]);
            const int dG = static_cast<int>(cand[1]) - static_cast<int>(ref[1]);
            const int dB = static_cast<int>(cand[2]) - static_cast<int>(ref[2]);
            const uint32_t aR = static_cast<uint32_t>(std::abs(dR));
            const uint32_t aG = static_cast<uint32_t>(std::abs(dG));
            const uint32_t aB = static_cast<uint32_t>(std::abs(dB));
            sumR += static_cast<double>(aR);
            sumG += static_cast<double>(aG);
            sumB += static_cast<double>(aB);
            sumSq += static_cast<double>(dR) * dR + static_cast<double>(dG) * dG + static_cast<double>(dB) * dB;
            maxAbs = std::max(maxAbs, std::max(aR, std::max(aG, aB)));
            if (aR > pixelThreshold || aG > pixelThreshold || aB > pixelThreshold)
            {
                ++mismatches;
            }
        }

        stats.maeR = sumR / static_cast<double>(pixelCount);
        stats.maeG = sumG / static_cast<double>(pixelCount);
        stats.maeB = sumB / static_cast<double>(pixelCount);
        stats.mae = (stats.maeR + stats.maeG + stats.maeB) / 3.0;
        stats.rmse = std::sqrt(sumSq / (static_cast<double>(pixelCount) * 3.0));
        stats.maxAbs = maxAbs;
        stats.mismatchCount = mismatches;
        stats.mismatchFraction = static_cast<double>(mismatches) / static_cast<double>(pixelCount);
        return stats;
    }

    bool StatsPass(const CompareStats& stats, double maxMae, double maxMismatchFraction)
    {
        return !stats.dimensionMismatch &&
            stats.mae <= maxMae &&
            stats.mismatchFraction <= maxMismatchFraction;
    }

    bool LoadCaptureIdentity(const std::filesystem::path& directory, CaptureIdentity& identity, std::string& error)
    {
        identity = {};
        const std::filesystem::path path = directory / kMetadataFileName;
        if (!std::filesystem::exists(path))
        {
            error = "Missing " + std::string(kMetadataFileName) + " under '" + directory.generic_string() + "'.";
            return false;
        }

        const std::string json = ReadTextFile(path, error);
        if (json.empty())
        {
            if (error.empty())
            {
                error = "capture-metadata.json under '" + directory.generic_string() + "' is empty.";
            }
            return false;
        }

        Json::Value root;
        if (!ParseJsonObject(json, root, error))
        {
            return false;
        }

        CaptureIdentity loaded;
        if (!RequireJsonString(root, "schema", loaded.schema, error) ||
            !RequireJsonString(root, "sceneId", loaded.sceneId, error) ||
            !RequireJsonString(root, "cameraPreset", loaded.cameraPreset, error) ||
            !RequireJsonString(root, "adapterName", loaded.adapterName, error) ||
            !RequireJsonString(root, "driverVersion", loaded.driverVersion, error) ||
            !RequireJsonUint(root, "width", loaded.width, error) ||
            !RequireJsonUint(root, "height", loaded.height, error) ||
            !RequireJsonUint(root, "frameIndex", loaded.frameIndex, error) ||
            !RequireJsonUint(root, "sampleCount", loaded.sampleCount, error))
        {
            return false;
        }
        if (loaded.width == 0 || loaded.height == 0)
        {
            error = "capture-metadata.json has invalid width/height.";
            return false;
        }

        loaded.hasMetadata = true;
        identity = std::move(loaded);
        return true;
    }

    bool IdentityMatchesLockedCapture(const CaptureIdentity& identity, std::string& error)
    {
        if (!identity.hasMetadata)
        {
            error = "Capture identity metadata is missing.";
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
        return true;
    }

    bool IdentitiesCompatible(const CaptureIdentity& candidate, const CaptureIdentity& reference, std::string& error)
    {
        if (!IdentityMatchesLockedCapture(reference, error))
        {
            error = "Reference " + error;
            return false;
        }
        if (!IdentityMatchesLockedCapture(candidate, error))
        {
            error = "Candidate " + error;
            return false;
        }
        return true;
    }

    bool AdaptersMatch(const CaptureIdentity& candidate, const CaptureIdentity& reference)
    {
        return candidate.hasMetadata && reference.hasMetadata &&
            !candidate.adapterName.empty() && !reference.adapterName.empty() &&
            candidate.adapterName == reference.adapterName &&
            candidate.driverVersion == reference.driverVersion;
    }

    DirectoryCompareResult CompareDirectories(
        const std::filesystem::path& candidateDir,
        const std::filesystem::path& referenceDir)
    {
        DirectoryCompareResult result;
        std::string error;

        const bool loadedReference = LoadCaptureIdentity(referenceDir, result.reference, error);
        if (!loadedReference)
        {
            result.verdict = Verdict::Error;
            result.summary = error;
            return result;
        }
        const bool loadedCandidate = LoadCaptureIdentity(candidateDir, result.candidate, error);
        if (!loadedCandidate)
        {
            result.verdict = Verdict::Error;
            result.summary = error;
            return result;
        }

        std::string identityError;
        result.identityMatches = IdentitiesCompatible(result.candidate, result.reference, identityError);
        result.adapterMatches = AdaptersMatch(result.candidate, result.reference);

        bool anyTightFail = false;
        bool anyPortabilityFail = false;
        bool anyError = false;

        for (uint32_t index = 0; index < kViewCount; ++index)
        {
            const ViewRule& rule = GetViewRule(static_cast<ViewId>(index));
            ViewCompareResult view;
            view.rule = rule;

            RgbImage candidateImage;
            RgbImage referenceImage;
            std::string loadError;
            const std::filesystem::path candidatePath = candidateDir / rule.fileName;
            const std::filesystem::path referencePath = referenceDir / rule.fileName;
            if (!LoadPngRgb(candidatePath, candidateImage, loadError))
            {
                view.message = loadError;
                anyError = true;
                result.views.push_back(std::move(view));
                continue;
            }
            if (!LoadPngRgb(referencePath, referenceImage, loadError))
            {
                view.message = loadError;
                anyError = true;
                result.views.push_back(std::move(view));
                continue;
            }

            if (candidateImage.width != kWidth || candidateImage.height != kHeight ||
                referenceImage.width != kWidth || referenceImage.height != kHeight)
            {
                view.message = "Image is not the locked 1280x720 golden resolution.";
                view.tight.dimensionMismatch = true;
                view.portability.dimensionMismatch = true;
                anyTightFail = true;
                anyPortabilityFail = true;
                result.views.push_back(std::move(view));
                continue;
            }

            view.tight = CompareRgb(candidateImage, referenceImage, rule.pixelThreshold);
            view.portability = CompareRgb(candidateImage, referenceImage, rule.portabilityPixelThreshold);
            view.passedTight = StatsPass(view.tight, rule.maxMae, rule.maxMismatchFraction);
            view.passedPortability = StatsPass(
                view.portability, rule.portabilityMaxMae, rule.portabilityMaxMismatchFraction);

            if (!view.passedTight)
            {
                anyTightFail = true;
                char buffer[256] = {};
                std::snprintf(
                    buffer,
                    sizeof(buffer),
                    "tight fail mae=%.4f mismatch=%.6f maxAbs=%u%s",
                    view.tight.mae,
                    view.tight.mismatchFraction,
                    view.tight.maxAbs,
                    rule.weakOracle ? " (weak oracle)" : "");
                view.message = buffer;
            }
            else
            {
                view.message = rule.weakOracle ? "pass (weak oracle)" : "pass";
            }
            if (!view.passedPortability)
            {
                anyPortabilityFail = true;
            }
            result.views.push_back(std::move(view));
        }

        if (anyError)
        {
            result.verdict = Verdict::Error;
            result.summary = "Failed to load one or more golden views.";
            return result;
        }
        if (!result.identityMatches)
        {
            result.verdict = Verdict::Regression;
            result.summary = identityError.empty()
                ? "Capture identity does not match the locked scene/camera/resolution/frame."
                : identityError;
            return result;
        }
        if (!anyTightFail)
        {
            result.verdict = Verdict::Pass;
            result.summary = result.adapterMatches
                ? "All views are within the renderer-regression tolerances."
                : "All views are within the renderer-regression tolerances on a different adapter/driver.";
            return result;
        }
        if (!result.adapterMatches && !anyPortabilityFail)
        {
            result.verdict = Verdict::Portability;
            result.summary =
                "Adapter/driver differs from the approved golden environment and images exceed the "
                "tight renderer tolerances but stay within the portability band. This is not classified "
                "as a renderer regression.";
            return result;
        }

        result.verdict = Verdict::Regression;
        result.summary = result.adapterMatches
            ? "One or more views exceed renderer-regression tolerances on the approved adapter/driver."
            : "One or more views exceed both renderer-regression and portability tolerances.";
        return result;
    }

    const char* VerdictName(Verdict verdict)
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

    int VerdictExitCode(Verdict verdict)
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

    std::string FormatReport(const DirectoryCompareResult& result)
    {
        std::ostringstream out;
        out << "{\n";
        out << "  \"verdict\": \"" << VerdictName(result.verdict) << "\",\n";
        out << "  \"summary\": \"" << JsonEscape(result.summary) << "\",\n";
        out << "  \"identityMatches\": " << (result.identityMatches ? "true" : "false") << ",\n";
        out << "  \"adapterMatches\": " << (result.adapterMatches ? "true" : "false") << ",\n";
        out << "  \"candidateAdapter\": \"" << JsonEscape(result.candidate.adapterName) << "\",\n";
        out << "  \"candidateDriver\": \"" << JsonEscape(result.candidate.driverVersion) << "\",\n";
        out << "  \"referenceAdapter\": \"" << JsonEscape(result.reference.adapterName) << "\",\n";
        out << "  \"referenceDriver\": \"" << JsonEscape(result.reference.driverVersion) << "\",\n";
        out << "  \"views\": [\n";
        for (std::size_t i = 0; i < result.views.size(); ++i)
        {
            const ViewCompareResult& view = result.views[i];
            out << "    {\n";
            out << "      \"fileName\": \"" << JsonEscape(view.rule.fileName) << "\",\n";
            out << "      \"weakOracle\": " << (view.rule.weakOracle ? "true" : "false") << ",\n";
            out << "      \"passedTight\": " << (view.passedTight ? "true" : "false") << ",\n";
            out << "      \"passedPortability\": " << (view.passedPortability ? "true" : "false") << ",\n";
            out << "      \"mae\": " << view.tight.mae << ",\n";
            out << "      \"maxAbs\": " << view.tight.maxAbs << ",\n";
            out << "      \"mismatchFraction\": " << view.tight.mismatchFraction << ",\n";
            out << "      \"message\": \"" << JsonEscape(view.message) << "\"\n";
            out << "    }";
            if (i + 1 < result.views.size())
            {
                out << ",";
            }
            out << "\n";
        }
        out << "  ]\n";
        out << "}\n";
        return out.str();
    }

    bool WriteReport(const std::filesystem::path& path, const DirectoryCompareResult& result, std::string& error)
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
        output << FormatReport(result);
        return true;
    }
}
