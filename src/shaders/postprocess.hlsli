#ifndef RENDERLAB_POSTPROCESS_HLSLI
#define RENDERLAB_POSTPROCESS_HLSLI

#include "postprocess_cb.h"

// S3.1 frozen tone-map math. Matches src/renderer/PostProcessContract.h and
// docs/postprocess.md. UE 5.8.1 citations live in those files; the frozen chain
// is UE's default "Filmic" output path.
//
// Row-vector algebra (docs/renderer-conventions.md section 2): multiply as
// mul(color, M). Every matrix below is the UE literal transposed into this
// convention, so mul(color, kX) equals UE's column-vector mul(X_UE, color).
//
// The chain output is display-referred linear Rec.709, NOT clamped to [0, 1];
// the SRGBA8_UNORM render target applies the pipeline's only output transfer
// (hardware sRGB OETF) and saturates on store.

static const float kFilmSlope = 0.88;
static const float kFilmToe = 0.55;
static const float kFilmShoulder = 0.26;
static const float kFilmBlackClip = 0.0;
static const float kFilmWhiteClip = 0.04;
static const float kBlueCorrectionAmount = 0.6;
static const float kExpandGamutAmount = 1.0;

static const float kRRTGlowGain = 0.05;
static const float kRRTGlowMid = 0.08;
static const float kRRTRedScale = 0.82;
static const float kRRTRedPivot = 0.03;
static const float kRRTRedHue = 0.0;
static const float kRRTRedWidth = 135.0;
static const float kPreDesaturate = 0.96;
static const float kPostDesaturate = 0.93;

static const float kFilmInMatch = 0.18;
static const float kFilmOutMatch = 0.18;
static const float kYcRadiusWeight = 1.75;
static const float kRad2Deg = 57.29577951308232;

static const float3x3 kSRGBToXYZ =
{
    0.4123907993, 0.2126390059, 0.0193308187,
    0.3575843394, 0.7151686788, 0.1191947798,
    0.1804807884, 0.0721923154, 0.9505321522,
};

static const float3x3 kXYZToSRGB =
{
    3.2409699419, -0.9692436363, 0.0556300797,
    -1.5373831776, 1.8759675015, -0.2039769589,
    -0.4986107603, 0.0415550574, 1.0569715142,
};

static const float3x3 kAP1ToXYZ =
{
    0.6624541811, 0.2722287168, -0.0055746495,
    0.1340042065, 0.6740817658, 0.0040607335,
    0.1561876870, 0.0536895174, 1.0103391003,
};

static const float3x3 kXYZToAP1 =
{
    1.6410233797, -0.6636628587, 0.0117218943,
    -0.3248032942, 1.6153315917, -0.0082844420,
    -0.2364246952, 0.0167563477, 0.9883948585,
};

static const float3x3 kAP0ToAP1 =
{
    1.4514393161, -0.0765537734, 0.0083161484,
    -0.2365107469, 1.1762296998, -0.0060324498,
    -0.2149285693, -0.0996759264, 0.9977163014,
};

static const float3x3 kAP1ToAP0 =
{
    0.6954522414, 0.0447945634, -0.0055258826,
    0.1406786965, 0.8596711185, 0.0040252103,
    0.1638690622, 0.0955343182, 1.0015006723,
};

static const float3x3 kD65ToD60CAT =
{
    1.0130349146, 0.0076982301, -0.0028413174,
    0.0061052578, 0.9981633521, 0.0046851567,
    -0.0149709436, -0.0050320385, 0.9245061375,
};

static const float3x3 kD60ToD65CAT =
{
    0.9872240087, -0.0075983718, 0.0030725771,
    -0.0061132286, 1.0018614847, -0.0050959615,
    0.0159532883, 0.0053300358, 1.0816806031,
};

static const float3x3 kBlueCorrect =
{
    0.9404372683, 0.0083786969, 0.0005471261,
    -0.0183068787, 0.8286599939, -0.0008833746,
    0.0778696104, 0.1629613092, 1.0003362486,
};

static const float3x3 kBlueCorrectInv =
{
    1.06318, -0.0106337, -0.000590887,
    0.0233956, 1.20632, 0.00105248,
    -0.0865726, -0.19569, 0.999538,
};

static const float3x3 kWideToXYZ =
{
    0.5441691, 0.2394656, -0.0023439,
    0.2395926, 0.7021530, 0.0361834,
    0.1666943, 0.0583814, 1.0552183,
};

static const float3 kAP1RGB2Y =
{
    0.2722287168,
    0.6740817658,
    0.0536895174,
};

// Composite matrices, composed in UE's apply order for the row-vector convention.
static const float3x3 kWorkingToAP1 = mul(kSRGBToXYZ, mul(kD65ToD60CAT, kXYZToAP1));
static const float3x3 kAP1ToWorking = mul(kAP1ToXYZ, mul(kD60ToD65CAT, kXYZToSRGB));
static const float3x3 kBlueCorrectAP1 = mul(kAP1ToAP0, mul(kBlueCorrect, kAP0ToAP1));
static const float3x3 kBlueCorrectInvAP1 = mul(kAP1ToAP0, mul(kBlueCorrectInv, kAP0ToAP1));
static const float3x3 kExpandWide = mul(kAP1ToWorking, mul(kWideToXYZ, kXYZToAP1));

float ExposureEVToScale(float exposureEV)
{
    return exp2(exposureEV);
}

float Square(float x)
{
    return x * x;
}

// UE ACESCommon.ush:140-145.
float RgbToSaturation(float3 rgb)
{
    float minRgb = min(min(rgb.r, rgb.g), rgb.b);
    float maxRgb = max(max(rgb.r, rgb.g), rgb.b);
    return (max(maxRgb, 1e-10) - max(minRgb, 1e-10)) / max(maxRgb, 1e-2);
}

// UE ACESCommon.ush:148-164. Neutral colors have hue 0.
float RgbToHue(float3 rgb)
{
    float hue = 0.0;
    if (!(rgb.r == rgb.g && rgb.g == rgb.b))
    {
        hue = kRad2Deg * atan2(sqrt(3.0) * (rgb.g - rgb.b), 2.0 * rgb.r - rgb.g - rgb.b);
    }
    if (hue < 0.0)
    {
        hue += 360.0;
    }
    return clamp(hue, 0.0, 360.0);
}

// UE ACESCommon.ush:166-172.
float CenterHue(float hue, float centerH)
{
    float hueCentered = hue - centerH;
    if (hueCentered < -180.0)
    {
        hueCentered += 360.0;
    }
    else if (hueCentered > 180.0)
    {
        hueCentered -= 360.0;
    }
    return hueCentered;
}

// UE ACESCommon.ush:268-290.
float RgbToYc(float3 rgb)
{
    float r = rgb.r;
    float g = rgb.g;
    float b = rgb.b;
    float chroma = sqrt(b * (b - g) + g * (g - r) + r * (r - b));
    return (b + g + r + kYcRadiusWeight * chroma) / 3.0;
}

// UE ACESCommon.ush:292-299.
float SigmoidShaper(float x)
{
    float t = max(1.0 - abs(0.5 * x), 0.0);
    float y = 1.0 + sign(x) * (1.0 - t * t);
    return 0.5 * y;
}

// UE ACESCommon.ush:234-249.
float GlowFwd(float ycIn, float glowGainIn, float glowMid)
{
    if (ycIn <= 2.0 / 3.0 * glowMid)
    {
        return glowGainIn;
    }
    if (ycIn >= 2.0 * glowMid)
    {
        return 0.0;
    }
    return glowGainIn * (glowMid / ycIn - 0.5);
}

// UE FilmToneMap (TonemapCommon.ush:110-225): AP1 in, AP1 out. The toe/shoulder
// blend uses guarded endpoints so exact black (log10(0) = -inf) does not hit
// inf * 0 = NaN inside lerp; identical to UE on its LUT domain.
float3 FilmToneMapAP1(float3 colorAP1)
{
    float3 colorAP0 = mul(colorAP1, kAP1ToAP0);

    float saturation = RgbToSaturation(colorAP0);
    float ycIn = RgbToYc(colorAP0);
    float s = SigmoidShaper((saturation - 0.4) / 0.2);
    float addedGlow = 1.0 + GlowFwd(ycIn, kRRTGlowGain * s, kRRTGlowMid);
    colorAP0 *= addedGlow;

    float hue = RgbToHue(colorAP0);
    float centeredHue = CenterHue(hue, kRRTRedHue);
    float hueWeight = Square(smoothstep(0.0, 1.0, 1.0 - abs(2.0 * centeredHue / kRRTRedWidth)));
    colorAP0.r += hueWeight * saturation * (kRRTRedPivot - colorAP0.r) * (1.0 - kRRTRedScale);

    float3 working = mul(colorAP0, kAP0ToAP1);
    working = max(working, 0.0);
    working = lerp(dot(working, kAP1RGB2Y).xxx, working, kPreDesaturate);

    const float toeScale = 1.0 + kFilmBlackClip - kFilmToe;
    const float shoulderScale = 1.0 + kFilmWhiteClip - kFilmShoulder;

    float toeMatch;
    if (kFilmToe > 0.8)
    {
        // 0.18 will be on the straight segment.
        toeMatch = (1.0 - kFilmToe - kFilmOutMatch) / kFilmSlope + log10(kFilmInMatch);
    }
    else
    {
        // Solve for ToeMatch such that input kFilmInMatch gives kFilmOutMatch.
        const float bt = (kFilmOutMatch + kFilmBlackClip) / toeScale - 1.0;
        toeMatch = log10(kFilmInMatch)
                 - 0.5 * log((1.0 + bt) / (1.0 - bt)) * (toeScale / kFilmSlope);
    }
    const float straightMatch = (1.0 - kFilmToe) / kFilmSlope - toeMatch;
    const float shoulderMatch = kFilmShoulder / kFilmSlope - straightMatch;

    float3 logColor = log10(working);
    float3 straightColor = kFilmSlope * (logColor + straightMatch);

    float3 toeColor = -kFilmBlackClip
        + (2.0 * toeScale) / (1.0 + exp((-2.0 * kFilmSlope / toeScale) * (logColor - toeMatch)));
    float3 shoulderColor = (1.0 + kFilmWhiteClip)
        - (2.0 * shoulderScale) / (1.0 + exp((2.0 * kFilmSlope / shoulderScale) * (logColor - shoulderMatch)));

    toeColor = (logColor < toeMatch) ? toeColor : straightColor;
    shoulderColor = (logColor > shoulderMatch) ? shoulderColor : straightColor;

    float3 t = saturate((logColor - toeMatch) / (shoulderMatch - toeMatch));
    t = (shoulderMatch < toeMatch) ? 1.0 - t : t;
    t = (3.0 - 2.0 * t) * t * t;

    // Guarded endpoints: t <= 0 selects toe, t >= 1 selects shoulder.
    float3 toneColor = (t <= 0.0) ? toeColor
        : ((t >= 1.0) ? shoulderColor : lerp(toeColor, shoulderColor, t));

    toneColor = lerp(dot(toneColor, kAP1RGB2Y).xxx, toneColor, kPostDesaturate);
    return max(toneColor, 0.0);
}

// UE expand gamut (PostProcessCombineLUTs.usf:377-401) with the documented
// non-positive-luminance guard.
float3 ExpandGamutAP1(float3 colorAP1)
{
    float luma = dot(colorAP1, kAP1RGB2Y);
    if (luma <= 0.0)
    {
        return colorAP1;
    }
    float3 chroma = colorAP1 / luma;
    float chromaDistSqr = dot(chroma - 1.0, chroma - 1.0);
    float expandAmount = (1.0 - exp2(-4.0 * chromaDistSqr))
        * (1.0 - exp2(-4.0 * kExpandGamutAmount * luma * luma));
    return lerp(colorAP1, mul(colorAP1, kExpandWide), expandAmount);
}

// The frozen output chain (docs/postprocess.md section 2). sceneColor is
// scene-referred linear Rec.709 (sRGB primaries, D65), exposure-independent.
float3 ApplyToneMapChain(float3 sceneColor, float exposureEV)
{
    float3 exposed = max(sceneColor, 0.0) * exp2(exposureEV);

    float3 colorAP1 = mul(exposed, kWorkingToAP1);
    colorAP1 = ExpandGamutAP1(colorAP1);
    colorAP1 = lerp(colorAP1, mul(colorAP1, kBlueCorrectAP1), kBlueCorrectionAmount);
    colorAP1 = FilmToneMapAP1(colorAP1);
    colorAP1 = lerp(colorAP1, mul(colorAP1, kBlueCorrectInvAP1), kBlueCorrectionAmount);
    return max(mul(colorAP1, kAP1ToWorking), 0.0);
}

float3 ApplyTonemapConstants(float3 sceneColor, TonemapConstants tonemap)
{
    return ApplyToneMapChain(sceneColor, tonemap.exposureEV);
}

#endif // RENDERLAB_POSTPROCESS_HLSLI
