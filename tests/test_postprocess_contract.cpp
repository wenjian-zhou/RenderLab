#include "renderer/PostProcessContract.h"

#include <cmath>
#include <cstdio>
#include <string>

// Reference expectations are computed independently in float64 by
// scripts/postprocess_reference.py (UE 5.8.1 chain reimplementation) and are
// not derived from PostProcessContract.h itself.

using namespace donut::math;
using namespace renderlab;

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

    bool Near(float actual, float expected, float tol)
    {
        return std::fabs(actual - expected) <= tol;
    }

    bool Near(const float3& actual, const float3& expected, float tol)
    {
        return Near(actual.x, expected.x, tol) &&
               Near(actual.y, expected.y, tol) &&
               Near(actual.z, expected.z, tol);
    }

    void CheckNearVec3(const float3& actual, const float3& expected, float tol, const char* message)
    {
        if (Near(actual, expected, tol))
        {
            std::printf("  PASS  %s\n", message);
            std::fflush(stdout);
            return;
        }
        std::printf("  FAIL  %s\n", message);
        std::printf("    actual   (%.9f, %.9f, %.9f)\n", actual.x, actual.y, actual.z);
        std::printf("    expected (%.9f, %.9f, %.9f)\n", expected.x, expected.y, expected.z);
        std::fflush(stdout);
        ++g_failures;
    }

    bool NearMatrix(const float3x3& actual, const float (&expected)[9], float tol)
    {
        const float values[9] = {
            actual.m00, actual.m01, actual.m02,
            actual.m10, actual.m11, actual.m12,
            actual.m20, actual.m21, actual.m22,
        };
        for (int i = 0; i < 9; ++i)
        {
            if (!Near(values[i], expected[i], tol))
            {
                std::printf("    matrix entry %d: actual %.9f expected %.9f\n",
                            i, values[i], expected[i]);
                return false;
            }
        }
        return true;
    }

    template <typename T, typename M>
    uint32_t FieldOffset(const T* object, const M* member)
    {
        return static_cast<uint32_t>(
            reinterpret_cast<const char*>(member) - reinterpret_cast<const char*>(object));
    }

    // fp32 evaluation of the full chain vs the float64 reference; observed
    // delta is ~1e-7, this leaves two orders of margin.
    constexpr float kChainTolerance = 1e-5f;
}

int RunPostProcessContractTests()
{
    std::printf("RenderLab S3.1 post-process contract tests\n");
    std::fflush(stdout);

    TonemapConstants tonemap = {};
    Check(sizeof(TonemapConstants) == 16, "TonemapConstants is 16 bytes");
    Check(FieldOffset(&tonemap, &tonemap.exposureEV) == kTonemapOffsetExposureEV,
          "exposureEV @ 0");
    Check(FieldOffset(&tonemap, &tonemap.pad0) == kTonemapOffsetPad0, "pad0 @ 4");
    Check(FieldOffset(&tonemap, &tonemap.pad1) == kTonemapOffsetPad1, "pad1 @ 8");
    Check(FieldOffset(&tonemap, &tonemap.pad2) == kTonemapOffsetPad2, "pad2 @ 12");
    Check(kTonemapConstantsByteSize == 16, "TonemapConstants byte size is 16");

    Check(Near(kDefaultExposureEV, 0.f, 0.f), "Default exposure EV is 0");
    const TonemapConstants defaults = MakeDefaultTonemapConstants();
    Check(Near(defaults.exposureEV, 0.f, 0.f), "Default tonemap constants use EV 0");
    Check(defaults.pad0 == 0 && defaults.pad1 == 0 && defaults.pad2 == 0,
          "Default tonemap constants leave pads zero");
    Check(std::string(kExposureEvCli) == "--exposure-ev", "exposure CLI name is --exposure-ev");

    Check(Near(ExposureEVToScale(0.f), 1.f, 0.f), "EV 0 is scale 1");
    Check(Near(ExposureEVToScale(1.f), 2.f, 1e-6f), "EV +1 is scale 2");
    Check(Near(ExposureEVToScale(-1.f), 0.5f, 1e-6f), "EV -1 is scale 0.5");
    Check(Near(ExposureEVToScale(3.f), 8.f, 1e-6f), "EV +3 is scale 8");
    Check(Near(ExposureEVToScale(-2.5f), 0.176776695f, 1e-6f), "EV -2.5 is scale 2^-2.5");

    Check(Near(kFilmSlope, 0.88f, 0.f) && Near(kFilmToe, 0.55f, 0.f) &&
              Near(kFilmShoulder, 0.26f, 0.f) && Near(kFilmBlackClip, 0.f, 0.f) &&
              Near(kFilmWhiteClip, 0.04f, 0.f),
          "Film curve constants match UE defaults (0.88/0.55/0.26/0/0.04)");
    Check(Near(kBlueCorrectionAmount, 0.6f, 0.f) && Near(kExpandGamutAmount, 1.f, 0.f),
          "Blue correction 0.6 and expand gamut 1.0 match UE defaults");
    Check(Near(kRRTGlowGain, 0.05f, 0.f) && Near(kRRTGlowMid, 0.08f, 0.f) &&
              Near(kRRTRedScale, 0.82f, 0.f) && Near(kRRTRedPivot, 0.03f, 0.f) &&
              Near(kRRTRedWidth, 135.f, 0.f),
          "RRT sweetener constants match UE");
    Check(Near(kPreDesaturate, 0.96f, 0.f) && Near(kPostDesaturate, 0.93f, 0.f) &&
              Near(kYcRadiusWeight, 1.75f, 0.f),
          "Desaturation and YC weight match UE");
    Check(Near(kFilmInMatch, 0.18f, 0.f) && Near(kFilmOutMatch, 0.18f, 0.f),
          "Log-curve match points are 0.18 in and out");

    Check(Near(kSRGBToXYZ.m00, 0.4123907993f, 1e-7f), "sRGB->XYZ literal spot check (m00)");
    Check(Near(kAP1ToAP0.m00, 0.6954522414f, 1e-7f), "AP1->AP0 literal spot check (m00)");
    Check(Near(kAP0ToAP1.m22, 0.9977163014f, 1e-7f), "AP0->AP1 literal spot check (m22)");
    Check(Near(kBlueCorrect.m10, -0.0183068787f, 1e-7f), "BlueCorrect literal spot check (m10)");
    Check(Near(kWideToXYZ.m20, 0.1666943f, 1e-7f), "Wide->XYZ literal spot check (m20)");
    Check(Near(kAP1RGB2Y.y, 0.6740817658f, 1e-7f), "AP1 RGB2Y matches AP1_2_XYZ row 1");

    // Composite matrices vs the independent float64 reference (scripts/postprocess_reference.py).
    const float expectedWorkingToAP1[9] = {
        0.613097402f, 0.070193723f, 0.020615593f,
        0.339523146f, 0.916353879f, 0.109569773f,
        0.047379451f, 0.013452399f, 0.869814634f};
    Check(NearMatrix(MakeWorkingToAP1(), expectedWorkingToAP1, 2e-6f),
          "Working->AP1 composite matches the reference (sRGB -> XYZ -> Bradford D65->D60 -> AP1)");

    const float expectedAP1ToWorking[9] = {
        1.705050993f, -0.130256417f, -0.024003357f,
        -0.621792120f, 1.140804736f, -0.128968976f,
        -0.083258872f, -0.010548319f, 1.152972333f};
    Check(NearMatrix(MakeAP1ToWorking(), expectedAP1ToWorking, 2e-6f),
          "AP1->working composite matches the reference");

    const float expectedBlueCorrectAP1[9] = {
        0.938639378f, 0.f, 0.f,
        0.f, 0.830794133f, 0.f,
        0.061360622f, 0.169205867f, 1.000000000f};
    Check(NearMatrix(MakeBlueCorrectAP1(), expectedBlueCorrectAP1, 2e-6f),
          "BlueCorrect conjugated into AP1 matches the reference");

    const float expectedBlueCorrectInvAP1[9] = {
        1.065374876f, -0.000000346f, 0.000000020f,
        0.000001447f, 1.203663524f, 0.000000021f,
        -0.065371005f, -0.203667720f, 0.999999600f};
    Check(NearMatrix(MakeBlueCorrectInvAP1(), expectedBlueCorrectInvAP1, 2e-6f),
          "BlueCorrectInv conjugated into AP1 matches the reference");

    const float expectedExpandWide[9] = {
        1.370412372f, -0.083433492f, -0.025793321f,
        -0.329292188f, 1.097092748f, -0.098625799f,
        -0.063683119f, -0.010861380f, 1.203694953f};
    Check(NearMatrix(MakeExpandWide(), expectedExpandWide, 2e-6f),
          "Expand-gamut matrix matches the reference");

    const float3x3 roundTrip = MakeWorkingToAP1() * MakeAP1ToWorking();
    const float3x3 blueRoundTrip = MakeBlueCorrectAP1() * MakeBlueCorrectInvAP1();
    Check(Near(roundTrip.m00, 1.f, 1e-5f) && Near(roundTrip.m11, 1.f, 1e-5f) &&
              Near(roundTrip.m22, 1.f, 1e-5f) && Near(roundTrip.m01, 0.f, 1e-5f) &&
              Near(roundTrip.m10, 0.f, 1e-5f) && Near(roundTrip.m20, 0.f, 1e-5f),
          "Working -> AP1 -> working round-trips to identity");
    Check(Near(blueRoundTrip.m00, 1.f, 1e-4f) && Near(blueRoundTrip.m11, 1.f, 1e-4f) &&
              Near(blueRoundTrip.m22, 1.f, 1e-4f) && Near(blueRoundTrip.m02, 0.f, 1e-4f),
          "BlueCorrect then BlueCorrectInv round-trips near identity (UE literals, 3e-6)");

    CheckNearVec3(FilmToneMapAP1(float3(0.18f)), float3(0.18f), 1e-6f,
                  "FilmToneMap keeps AP1 0.18 neutral at 0.18 (InMatch = OutMatch)");

    struct ChainCase
    {
        const char* name;
        float3 input;
        float ev;
        float3 expected;
    };
    const ChainCase cases[] = {
        {"midgray 0.18 EV+0", float3(0.18f), 0.f,
         float3(0.180001287f, 0.179999366f, 0.180000005f)},
        {"gray 0.05 EV+0", float3(0.05f), 0.f,
         float3(0.024837671f, 0.024837406f, 0.024837494f)},
        {"gray 1 EV+0", float3(1.f), 0.f,
         float3(0.723364630f, 0.723356909f, 0.723359476f)},
        {"gray 4 EV+0", float3(4.f), 0.f,
         float3(0.944161583f, 0.944151506f, 0.944154856f)},
        {"gray 16 EV+0", float3(16.f), 0.f,
         float3(1.014192122f, 1.014181298f, 1.014184896f)},
        {"red EV+0", float3(1.f, 0.f, 0.f), 0.f,
         float3(0.845248773f, 0.000000000f, 0.002348263f)},
        {"green EV+0", float3(0.f, 1.f, 0.f), 0.f,
         float3(0.000000000f, 0.814835467f, 0.000000000f)},
        {"blue EV+0", float3(0.f, 0.f, 1.f), 0.f,
         float3(0.000000000f, 0.002177125f, 0.734005563f)},
        {"cyan EV+0", float3(0.f, 1.f, 1.f), 0.f,
         float3(0.119936973f, 0.747414766f, 0.731097955f)},
        {"magenta EV+0", float3(1.f, 0.f, 1.f), 0.f,
         float3(0.891805747f, 0.041984853f, 0.743212179f)},
        {"yellow EV+0", float3(1.f, 1.f, 0.f), 0.f,
         float3(0.752823689f, 0.779152763f, 0.002656386f)},
        {"scene-referred gray-green EV+0", float3(0.2126f, 0.7152f, 0.0722f), 0.f,
         float3(0.254527722f, 0.676481923f, 0.082091608f)},
        {"teal HDR EV+0", float3(0.1f, 0.4f, 0.9f), 0.f,
         float3(0.111821862f, 0.465134724f, 0.701113508f)},
        {"blue HDR EV+0", float3(0.1f, 0.3f, 6.f), 0.f,
         float3(0.361649975f, 0.680850689f, 0.997684565f)},
        {"orange HDR EV+0", float3(8.f, 0.5f, 0.1f), 0.f,
         float3(1.207500258f, 0.664856487f, 0.170607272f)},
        {"midgray 0.18 EV+1", float3(0.18f), 1.f,
         float3(0.401680984f, 0.401676697f, 0.401678122f)},
        {"midgray 0.18 EV-2", float3(0.18f), -2.f,
         float3(0.020862041f, 0.020861818f, 0.020861892f)},
        {"teal HDR EV+2", float3(0.1f, 0.4f, 0.9f), 2.f,
         float3(0.510481593f, 0.852944719f, 0.946903641f)},
        {"black EV+0", float3(0.f), 0.f, float3(0.f)},
    };

    bool inBounds = true;
    for (const ChainCase& testCase : cases)
    {
        const float3 out = ApplyToneMapChain(testCase.input, testCase.ev);
        CheckNearVec3(out, testCase.expected, kChainTolerance, testCase.name);
        if (out.x < -1e-6f || out.x > 1.25f || out.y < -1e-6f || out.y > 1.25f ||
            out.z < -1e-6f || out.z > 1.25f)
        {
            inBounds = false;
        }
    }
    Check(inBounds, "All reference outputs stay within [0, 1.25] (not clamped; RTV saturates)");

    const float3 black = ApplyToneMapChain(float3(0.f, 0.f, 0.f), 0.f);
    Check(black.x == 0.f && black.y == 0.f && black.z == 0.f,
          "Black maps to exactly zero (no NaN from log10(0) or the 0/0 chroma)");

    const float3 midGray = ApplyToneMapChain(float3(0.18f), 0.f);
    CheckNearVec3(midGray, float3(0.18f), 2e-5f,
                  "Mid-gray 0.18 is a near fixed point of the whole chain");

    bool monotonic = true;
    float previous = -1.f;
    for (float g : {0.001f, 0.01f, 0.05f, 0.1f, 0.18f, 0.5f, 1.f, 2.f, 4.f, 8.f, 16.f, 50.f, 100.f})
    {
        const float out = ApplyToneMapChain(float3(g), 0.f).y;
        if (out < previous)
        {
            monotonic = false;
        }
        previous = out;
    }
    Check(monotonic, "Gray ramp 0.001..100 is non-decreasing");

    const float3 evEquivalenceA = ApplyToneMapChain(float3(0.1f, 0.4f, 0.9f), 1.f);
    const float3 evEquivalenceB = ApplyToneMapChain(float3(0.2f, 0.8f, 1.8f), 0.f);
    Check(Near(evEquivalenceA, evEquivalenceB, 1e-6f),
          "EV +1 equals doubling the input (exposure is a pre-multiply)");

    return g_failures;
}
