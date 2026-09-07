#pragma once

#include "shaders/postprocess_cb.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// S3.1 exposure and tone-map contract. Mirrors src/shaders/postprocess.hlsli and
// docs/postprocess.md. The curve is UE 5.8.1's default "Filmic" chain, frozen:
//
//   FilmToneMap            TonemapCommon.ush:110-225
//   Base matrices/helpers  ACES/ACESCommon.ush
//   BlueCorrect/ExpandGamut/working-space conversion
//                          PostProcessCombineLUTs.usf:160-234, 344-481
//   WorkingColorSpace.ToAP1 (sRGB -> AP1 incl. Bradford D65->D60 CAT)
//                          SceneManagement.cpp:154-155, ColorSpace.cpp:371-382,
//                          ColorManagementDefines.h:110
//   Film defaults          Scene.cpp:436-449
//
// UE stages that are exact identities under default settings are not part of
// this contract: default grading (ColorCorrectAll / ColorCorrection /
// ColorScale / OverlayColor), pow(color, 2.2/2.2), and the AP1 -> output-gamut
// matrix short-circuit for sRGB working space + sRGB D65 output
// (PostProcessCombineLUTs.usf:309-311).
//
// Documented RenderLab adaptations (docs/postprocess.md section 6):
//   1. Matrices are transposed into the row-vector, row-major convention of
//      docs/renderer-conventions.md section 2; multiply as color * M.
//   2. Expand gamut is skipped when AP1 luminance <= 0. UE only evaluates the
//      strictly positive LUT grid; per-pixel black would be 0/0 = NaN.
//   3. The toe/shoulder blend uses guarded endpoints (t <= 0 -> toe,
//      t >= 1 -> shoulder) so exact black (log10(0) = -inf) does not hit
//      inf * 0 = NaN inside lerp. Identical to UE on UE's LUT domain.
//   4. Evaluation is fp32. UE uses half in the log-curve section and quantizes
//      through the 32^3 grading LUT.
//
// The chain output is display-referred linear Rec.709 and is NOT clamped to
// [0, 1] (the shoulder asymptote is 1 + whiteClip, blue uncorrection can
// overshoot); the SRGBA8_UNORM back buffer saturates on store. The hardware
// sRGB OETF on that target is the pipeline's only output transfer.

namespace renderlab
{
    inline constexpr uint32_t kTonemapConstantsByteSize = 16;

    static_assert(sizeof(TonemapConstants) == kTonemapConstantsByteSize);
    static_assert(kTonemapConstantsByteSize % 16 == 0);

    inline constexpr uint32_t kTonemapOffsetExposureEV = 0;
    inline constexpr uint32_t kTonemapOffsetPad0 = 4;
    inline constexpr uint32_t kTonemapOffsetPad1 = 8;
    inline constexpr uint32_t kTonemapOffsetPad2 = 12;

    inline constexpr float kDefaultExposureEV = 0.f;
    inline constexpr const char* kExposureEvCli = "--exposure-ev";

    // UE 5.8.1 frozen film-curve defaults (Scene.cpp:436-449) and chain amounts
    // (PostProcessCombineLUTs.usf:156-158).
    inline constexpr float kFilmSlope = 0.88f;
    inline constexpr float kFilmToe = 0.55f;
    inline constexpr float kFilmShoulder = 0.26f;
    inline constexpr float kFilmBlackClip = 0.0f;
    inline constexpr float kFilmWhiteClip = 0.04f;
    inline constexpr float kBlueCorrectionAmount = 0.6f;
    inline constexpr float kExpandGamutAmount = 1.0f;

    // RRT sweeteners (TonemapCommon.ush:150-172) and desaturation
    // (TonemapCommon.ush:180, 221).
    inline constexpr float kRRTGlowGain = 0.05f;
    inline constexpr float kRRTGlowMid = 0.08f;
    inline constexpr float kRRTRedScale = 0.82f;
    inline constexpr float kRRTRedPivot = 0.03f;
    inline constexpr float kRRTRedHue = 0.0f;
    inline constexpr float kRRTRedWidth = 135.0f;
    inline constexpr float kPreDesaturate = 0.96f;
    inline constexpr float kPostDesaturate = 0.93f;

    // Log-curve match points (TonemapCommon.ush:185-186) and the YC chroma
    // radius weight (ACESCommon.ush:268 default).
    inline constexpr float kFilmInMatch = 0.18f;
    inline constexpr float kFilmOutMatch = 0.18f;
    inline constexpr float kYcRadiusWeight = 1.75f;
    inline constexpr float kRad2Deg = 57.29577951308232f;

    // Base matrices: UE 5.8.1 literals transposed to row-major, row-vector
    // convention (renderer-conventions.md section 2), so `color * kX` equals
    // UE's column-vector `mul(X_UE, color)`. Sources: ACES/ACESCommon.ush:17-79,
    // 112-124; PostProcessCombineLUTs.usf:165-176, 390-395.
    inline constexpr donut::math::float3x3 kSRGBToXYZ = donut::math::float3x3(
        0.4123907993f, 0.2126390059f, 0.0193308187f,
        0.3575843394f, 0.7151686788f, 0.1191947798f,
        0.1804807884f, 0.0721923154f, 0.9505321522f);

    inline constexpr donut::math::float3x3 kXYZToSRGB = donut::math::float3x3(
        3.2409699419f, -0.9692436363f, 0.0556300797f,
        -1.5373831776f, 1.8759675015f, -0.2039769589f,
        -0.4986107603f, 0.0415550574f, 1.0569715142f);

    inline constexpr donut::math::float3x3 kAP1ToXYZ = donut::math::float3x3(
        0.6624541811f, 0.2722287168f, -0.0055746495f,
        0.1340042065f, 0.6740817658f, 0.0040607335f,
        0.1561876870f, 0.0536895174f, 1.0103391003f);

    inline constexpr donut::math::float3x3 kXYZToAP1 = donut::math::float3x3(
        1.6410233797f, -0.6636628587f, 0.0117218943f,
        -0.3248032942f, 1.6153315917f, -0.0082844420f,
        -0.2364246952f, 0.0167563477f, 0.9883948585f);

    inline constexpr donut::math::float3x3 kAP0ToAP1 = donut::math::float3x3(
        1.4514393161f, -0.0765537734f, 0.0083161484f,
        -0.2365107469f, 1.1762296998f, -0.0060324498f,
        -0.2149285693f, -0.0996759264f, 0.9977163014f);

    inline constexpr donut::math::float3x3 kAP1ToAP0 = donut::math::float3x3(
        0.6954522414f, 0.0447945634f, -0.0055258826f,
        0.1406786965f, 0.8596711185f, 0.0040252103f,
        0.1638690622f, 0.0955343182f, 1.0015006723f);

    inline constexpr donut::math::float3x3 kD65ToD60CAT = donut::math::float3x3(
        1.0130349146f, 0.0076982301f, -0.0028413174f,
        0.0061052578f, 0.9981633521f, 0.0046851567f,
        -0.0149709436f, -0.0050320385f, 0.9245061375f);

    inline constexpr donut::math::float3x3 kD60ToD65CAT = donut::math::float3x3(
        0.9872240087f, -0.0075983718f, 0.0030725771f,
        -0.0061132286f, 1.0018614847f, -0.0050959615f,
        0.0159532883f, 0.0053300358f, 1.0816806031f);

    inline constexpr donut::math::float3x3 kBlueCorrect = donut::math::float3x3(
        0.9404372683f, 0.0083786969f, 0.0005471261f,
        -0.0183068787f, 0.8286599939f, -0.0008833746f,
        0.0778696104f, 0.1629613092f, 1.0003362486f);

    inline constexpr donut::math::float3x3 kBlueCorrectInv = donut::math::float3x3(
        1.06318f, -0.0106337f, -0.000590887f,
        0.0233956f, 1.20632f, 0.00105248f,
        -0.0865726f, -0.19569f, 0.999538f);

    inline constexpr donut::math::float3x3 kWideToXYZ = donut::math::float3x3(
        0.5441691f, 0.2394656f, -0.0023439f,
        0.2395926f, 0.7021530f, 0.0361834f,
        0.1666943f, 0.0583814f, 1.0552183f);

    // AP1 luminance row (ACESCommon.ush:59-64).
    inline constexpr donut::math::float3 kAP1RGB2Y =
        donut::math::float3(0.2722287168f, 0.6740817658f, 0.0536895174f);

    // Composite matrices, composed in the same apply order as UE's column-vector
    // chain (docs/postprocess.md section 3). Row-vector algebra: a * b applies
    // a first, matching matrix.h operator*.
    inline donut::math::float3x3 MakeWorkingToAP1()
    {
        return kSRGBToXYZ * kD65ToD60CAT * kXYZToAP1;
    }

    inline donut::math::float3x3 MakeAP1ToWorking()
    {
        return kAP1ToXYZ * kD60ToD65CAT * kXYZToSRGB;
    }

    inline donut::math::float3x3 MakeBlueCorrectAP1()
    {
        return kAP1ToAP0 * kBlueCorrect * kAP0ToAP1;
    }

    inline donut::math::float3x3 MakeBlueCorrectInvAP1()
    {
        return kAP1ToAP0 * kBlueCorrectInv * kAP0ToAP1;
    }

    inline donut::math::float3x3 MakeExpandWide()
    {
        return MakeAP1ToWorking() * (kWideToXYZ * kXYZToAP1);
    }

    inline TonemapConstants MakeDefaultTonemapConstants()
    {
        TonemapConstants tonemap = {};
        tonemap.exposureEV = kDefaultExposureEV;
        return tonemap;
    }

    // Manual exposure in EV stops, UE AutoExposureBias semantics
    // (Scene.h:1928-1933; manual mode is calibrated to 1.0,
    // PostProcessEyeAdaptation.cpp:626): +1 EV doubles brightness, no clamp.
    inline float ExposureEVToScale(float exposureEV)
    {
        return std::exp2f(exposureEV);
    }

    inline float Square(float x)
    {
        return x * x;
    }

    inline float SignF(float x)
    {
        return (x > 0.f) ? 1.f : ((x < 0.f) ? -1.f : 0.f);
    }

    inline float Smoothstep01(float x)
    {
        const float t = std::min(std::max(x, 0.f), 1.f);
        return t * t * (3.f - 2.f * t);
    }

    // UE ACESCommon.ush:140-145.
    inline float RgbToSaturation(const donut::math::float3& rgb)
    {
        const float minRgb = std::min(std::min(rgb.x, rgb.y), rgb.z);
        const float maxRgb = std::max(std::max(rgb.x, rgb.y), rgb.z);
        return (std::max(maxRgb, 1e-10f) - std::max(minRgb, 1e-10f)) / std::max(maxRgb, 1e-2f);
    }

    // UE ACESCommon.ush:148-164. Neutral colors have hue 0.
    inline float RgbToHue(const donut::math::float3& rgb)
    {
        float hue = 0.f;
        if (!(rgb.x == rgb.y && rgb.y == rgb.z))
        {
            hue = kRad2Deg * std::atan2f(
                std::sqrt(3.f) * (rgb.y - rgb.z), 2.f * rgb.x - rgb.y - rgb.z);
        }
        if (hue < 0.f)
        {
            hue += 360.f;
        }
        return std::min(std::max(hue, 0.f), 360.f);
    }

    // UE ACESCommon.ush:166-172.
    inline float CenterHue(float hue, float centerH)
    {
        float hueCentered = hue - centerH;
        if (hueCentered < -180.f)
        {
            hueCentered += 360.f;
        }
        else if (hueCentered > 180.f)
        {
            hueCentered -= 360.f;
        }
        return hueCentered;
    }

    // UE ACESCommon.ush:268-290.
    inline float RgbToYc(const donut::math::float3& rgb)
    {
        const float r = rgb.x;
        const float g = rgb.y;
        const float b = rgb.z;
        const float chroma = std::sqrtf(b * (b - g) + g * (g - r) + r * (r - b));
        return (b + g + r + kYcRadiusWeight * chroma) / 3.f;
    }

    // UE ACESCommon.ush:292-299.
    inline float SigmoidShaper(float x)
    {
        const float t = std::max(1.f - std::fabs(0.5f * x), 0.f);
        const float y = 1.f + SignF(x) * (1.f - t * t);
        return 0.5f * y;
    }

    // UE ACESCommon.ush:234-249.
    inline float GlowFwd(float ycIn, float glowGainIn, float glowMid)
    {
        if (ycIn <= 2.f / 3.f * glowMid)
        {
            return glowGainIn;
        }
        if (ycIn >= 2.f * glowMid)
        {
            return 0.f;
        }
        return glowGainIn * (glowMid / ycIn - 0.5f);
    }

    inline donut::math::float3 Max0(const donut::math::float3& v)
    {
        return donut::math::float3(
            std::max(v.x, 0.f), std::max(v.y, 0.f), std::max(v.z, 0.f));
    }

    // UE FilmToneMap (TonemapCommon.ush:110-225): AP1 in, AP1 out, with the
    // documented black-safe blend. All curve parameters are the frozen
    // constants above.
    inline donut::math::float3 FilmToneMapAP1(const donut::math::float3& colorAP1)
    {
        donut::math::float3 colorAP0 = colorAP1 * kAP1ToAP0;

        const float saturation = RgbToSaturation(colorAP0);
        const float ycIn = RgbToYc(colorAP0);
        const float s = SigmoidShaper((saturation - 0.4f) / 0.2f);
        const float addedGlow = 1.f + GlowFwd(ycIn, kRRTGlowGain * s, kRRTGlowMid);
        colorAP0 = colorAP0 * addedGlow;

        const float hue = RgbToHue(colorAP0);
        const float centeredHue = CenterHue(hue, kRRTRedHue);
        const float hueWeight =
            Square(Smoothstep01(1.f - std::fabs(2.f * centeredHue / kRRTRedWidth)));
        colorAP0.x += hueWeight * saturation * (kRRTRedPivot - colorAP0.x) * (1.f - kRRTRedScale);

        donut::math::float3 working = colorAP0 * kAP0ToAP1;
        working = Max0(working);
        const float workingLuma = dot(working, kAP1RGB2Y);
        working = donut::math::lerp(donut::math::float3(workingLuma), working, kPreDesaturate);

        const float toeScale = 1.f + kFilmBlackClip - kFilmToe;
        const float shoulderScale = 1.f + kFilmWhiteClip - kFilmShoulder;

        float toeMatch;
        if constexpr (kFilmToe > 0.8f)
        {
            toeMatch = (1.f - kFilmToe - kFilmOutMatch) / kFilmSlope + std::log10f(kFilmInMatch);
        }
        else
        {
            const float bt = (kFilmOutMatch + kFilmBlackClip) / toeScale - 1.f;
            toeMatch = std::log10f(kFilmInMatch)
                     - 0.5f * std::logf((1.f + bt) / (1.f - bt)) * (toeScale / kFilmSlope);
        }
        const float straightMatch = (1.f - kFilmToe) / kFilmSlope - toeMatch;
        const float shoulderMatch = kFilmShoulder / kFilmSlope - straightMatch;

        donut::math::float3 logColor = donut::math::float3(
            std::log10f(working.x), std::log10f(working.y), std::log10f(working.z));

        donut::math::float3 toeColor;
        donut::math::float3 shoulderColor;
        donut::math::float3 t;
        for (uint32_t i = 0; i < 3; ++i)
        {
            const float lc = logColor[i];
            const float straightVal = kFilmSlope * (lc + straightMatch);

            const float toeVal = -kFilmBlackClip + (2.f * toeScale)
                / (1.f + std::expf((-2.f * kFilmSlope / toeScale) * (lc - toeMatch)));
            toeColor[i] = (lc < toeMatch) ? toeVal : straightVal;

            const float shoulderVal = (1.f + kFilmWhiteClip) - (2.f * shoulderScale)
                / (1.f + std::expf((2.f * kFilmSlope / shoulderScale) * (lc - shoulderMatch)));
            shoulderColor[i] = (lc > shoulderMatch) ? shoulderVal : straightVal;

            float ti = (lc - toeMatch) / (shoulderMatch - toeMatch);
            ti = std::min(std::max(ti, 0.f), 1.f);
            ti = (shoulderMatch < toeMatch) ? 1.f - ti : ti;
            t[i] = (3.f - 2.f * ti) * ti * ti;
        }

        donut::math::float3 toneColor;
        for (uint32_t i = 0; i < 3; ++i)
        {
            toneColor[i] = (t[i] <= 0.f) ? toeColor[i]
                : ((t[i] >= 1.f) ? shoulderColor[i]
                                 : toeColor[i] + t[i] * (shoulderColor[i] - toeColor[i]));
        }

        const float toneLuma = dot(toneColor, kAP1RGB2Y);
        toneColor = donut::math::lerp(donut::math::float3(toneLuma), toneColor, kPostDesaturate);
        return Max0(toneColor);
    }

    // UE expand gamut (PostProcessCombineLUTs.usf:377-401) with the documented
    // non-positive-luminance guard.
    inline donut::math::float3 ExpandGamutAP1(const donut::math::float3& colorAP1)
    {
        const float luma = dot(colorAP1, kAP1RGB2Y);
        if (luma <= 0.f)
        {
            return colorAP1;
        }
        const donut::math::float3 chroma = colorAP1 / luma;
        const donut::math::float3 chromaMinusOne = chroma - donut::math::float3(1.f);
        const float chromaDistSqr = dot(chromaMinusOne, chromaMinusOne);
        const float expandAmount = (1.f - std::exp2f(-4.f * chromaDistSqr))
            * (1.f - std::exp2f(-4.f * kExpandGamutAmount * luma * luma));
        return donut::math::lerp(colorAP1, colorAP1 * MakeExpandWide(), expandAmount);
    }

    // The frozen output chain (docs/postprocess.md section 2). sceneColor is
    // scene-referred linear Rec.709 (sRGB primaries, D65), exposure-independent.
    // The result is display-referred linear Rec.709, not clamped to [0, 1];
    // the sRGB RTV applies the only output transfer and saturates on store.
    inline donut::math::float3 ApplyToneMapChain(
        const donut::math::float3& sceneColor, float exposureEV)
    {
        const donut::math::float3 exposed =
            Max0(sceneColor) * ExposureEVToScale(exposureEV);

        donut::math::float3 colorAP1 = exposed * MakeWorkingToAP1();
        colorAP1 = ExpandGamutAP1(colorAP1);
        colorAP1 = donut::math::lerp(
            colorAP1, colorAP1 * MakeBlueCorrectAP1(), kBlueCorrectionAmount);
        colorAP1 = FilmToneMapAP1(colorAP1);
        colorAP1 = donut::math::lerp(
            colorAP1, colorAP1 * MakeBlueCorrectInvAP1(), kBlueCorrectionAmount);
        return Max0(colorAP1 * MakeAP1ToWorking());
    }
}
