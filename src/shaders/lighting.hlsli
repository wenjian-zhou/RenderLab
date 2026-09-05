#ifndef RENDERLAB_LIGHTING_HLSLI
#define RENDERLAB_LIGHTING_HLSLI

#include "renderer_cb.h"

// Shared lighting helpers. Matches src/renderer/LightingContract.h and docs/lighting.md.
// Microfacet terms follow UE DefaultLit (BRDF.ush / ShadingModels.ush /
// ShadingEnergyConservation.ush analytic path). Callers must reject background
// pixels (deviceDepth <= 0) before reconstructing.

static const float kPi = 3.14159265358979323846;
static const float kDielectricF0 = 0.04;
static const float kMinGGXAlpha = 1e-3;
static const float kPointLightMinDistanceSq = 1e-4;
static const float kSpecularDenomEpsilon = 1e-5;
static const float kMinEnergyRoughness = 1e-4;

bool IsBackgroundDeviceDepth(float deviceDepth)
{
    return deviceDepth <= 0.0;
}

float3 ReconstructWorldPosition(float2 pixelPosition, float deviceDepth, ViewConstants view)
{
    const float2 uv = (pixelPosition - view.viewportOrigin) * view.viewportSizeInv;
    const float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, deviceDepth, 1.0);
    const float4 worldPosH = mul(clipPos, view.matClipToWorld);
    return worldPosH.xyz / worldPosH.w;
}

float3 ComputeDiffuseAlbedo(float3 baseColor, float metallic)
{
    return baseColor * (1.0 - metallic);
}

float3 ComputeF0(float3 baseColor, float metallic)
{
    return lerp(float3(kDielectricF0, kDielectricF0, kDielectricF0), baseColor, metallic);
}

float PerceptualRoughnessToAlpha(float roughness)
{
    return max(roughness * roughness, kMinGGXAlpha);
}

float PointLightAttenuation(float distance, float range)
{
    if (range <= 0.0 || distance >= range)
    {
        return 0.0;
    }
    return 1.0 / max(distance * distance, kPointLightMinDistanceSq);
}

float Pow5(float x)
{
    const float xx = x * x;
    return xx * xx * x;
}

float LuminanceRec709(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

// UE F0RGBToMicroOcclusion: saturate(50 * max3(F0)).
float F0RGBToMicroOcclusion(float3 f0)
{
    return saturate(50.0 * max(f0.r, max(f0.g, f0.b)));
}

float D_GGX(float NdotH, float alpha)
{
    const float a2 = alpha * alpha;
    const float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * d * d, kSpecularDenomEpsilon);
}

float Vis_SmithJointApprox(float NdotL, float NdotV, float alpha)
{
    const float a = alpha;
    return 0.5 / max(
        NdotL * (NdotV * (1.0 - a) + a) + NdotV * (NdotL * (1.0 - a) + a),
        kSpecularDenomEpsilon);
}

// UE BRDF.ush F_Schlick(SpecularColor, VoH): <2% F0.g treated as shadowing.
float3 F_Schlick(float3 f0, float VdotH)
{
    const float Fc = Pow5(saturate(1.0 - VdotH));
    return saturate(50.0 * f0.g) * Fc + (1.0 - Fc) * f0;
}

// UE ShadingEnergyConservation.ush GGXEnergyLookup with USE_ENERGY_CONSERVATION == 2.
float2 GGXEnergyLookupAnalytic(float roughness, float NdotV)
{
    const float r = max(roughness, kMinEnergyRoughness);
    const float c = NdotV;
    const float E = 1.0 - saturate(pow(r, c / r) * ((r * c + 0.0266916) / (0.466495 + c)));
    const float Ef = Pow5(1.0 - c) * pow(2.36651 * pow(c, 4.7703 * r) + 0.0387332, r);
    return float2(E, Ef);
}

// Returns (diffuse + specular) * saturate(NdotL). Caller multiplies color * intensity * att * Vk.
// Diffuse/specular split matches UE DefaultLitBxDF with analytic energy terms (no LUT).
float3 EvaluateDirectBRDF(
    float3 diffuseAlbedo,
    float3 f0,
    float alpha,
    float perceptualRoughness,
    float3 N,
    float3 V,
    float3 L)
{
    const float NdotL = saturate(dot(N, L));
    if (NdotL <= 0.0)
    {
        return float3(0.0, 0.0, 0.0);
    }

    const float3 H = normalize(V + L);
    // UE DefaultLit: saturate(abs(NoV) + 1e-5).
    const float NdotV = saturate(abs(dot(N, V)) + 1e-5);
    const float NdotH = saturate(dot(N, H));
    const float VdotH = saturate(dot(V, H));

    const float3 fd = diffuseAlbedo / kPi;
    const float D = D_GGX(NdotH, alpha);
    const float Vis = Vis_SmithJointApprox(NdotL, NdotV, alpha);
    const float3 F = F_Schlick(f0, VdotH);

    float3 diffuse = fd;
    float3 specular = D * Vis * F;

    const float2 E = GGXEnergyLookupAnalytic(perceptualRoughness, NdotV);
    const float F90 = F0RGBToMicroOcclusion(f0);
    const float safeE = max(E.x, kSpecularDenomEpsilon);
    const float3 W = 1.0 + f0 * ((1.0 - E.x) / safeE);
    const float3 reflectionE = W * (E.x * f0 + E.y * (F90 - f0));
    const float preservation = saturate(1.0 - LuminanceRec709(reflectionE));

    diffuse *= preservation;
    specular *= W;

    return (diffuse + specular) * NdotL;
}

#endif // RENDERLAB_LIGHTING_HLSLI
