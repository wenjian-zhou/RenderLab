#include "renderer_cb.h"
#include "lighting_cb.h"
#include "lighting.hlsli"

// S2.3 deferred lighting. Writes scene-referred HDRSceneColor.
// Lambert + UE DefaultLit GGX; one directional + pointLightCount points + ambient.
// Visualization encodings are not written here; see lighting_debug_ps.hlsl.

ConstantBuffer<ViewConstants> g_View : register(b0);
ConstantBuffer<LightingConstants> g_Lighting : register(b1);

Texture2D t_GBufferA : register(t0);
Texture2D t_GBufferB : register(t1);
Texture2D t_GBufferC : register(t2);
Texture2D t_GBufferDepth : register(t3); // R32_FLOAT SRV on the typeless D32 texture

float4 main(float4 position : SV_Position) : SV_Target0
{
    const float2 pixelPosition = position.xy;
    const int2 pixelCoord = int2(pixelPosition);

    const float4 targetA = t_GBufferA.Load(int3(pixelCoord, 0));
    const float4 targetB = t_GBufferB.Load(int3(pixelCoord, 0));
    const float4 targetC = t_GBufferC.Load(int3(pixelCoord, 0));
    const float deviceDepth = t_GBufferDepth.Load(int3(pixelCoord, 0)).r;

    if (IsBackgroundDeviceDepth(deviceDepth))
    {
        return float4(g_Lighting.backgroundRadiance, 1.0);
    }

    const float3 worldPos = ReconstructWorldPosition(pixelPosition, deviceDepth, g_View);
    if (!(worldPos.x == worldPos.x) || !(worldPos.y == worldPos.y) || !(worldPos.z == worldPos.z) ||
        isinf(worldPos.x) || isinf(worldPos.y) || isinf(worldPos.z))
    {
        return float4(g_Lighting.backgroundRadiance, 1.0);
    }

    const float3 baseColor = targetA.rgb;
    const float3 N = normalize(targetB.rgb);
    const float roughness = targetB.a;
    const float metallic = targetC.r;
    const float ao = targetC.g;

    const float3 diffuseAlbedo = ComputeDiffuseAlbedo(baseColor, metallic);
    const float3 f0 = ComputeF0(baseColor, metallic);
    const float alpha = PerceptualRoughnessToAlpha(roughness);
    const float3 V = normalize(g_View.cameraPosition.xyz - worldPos);

    // Ambient: AO multiplies ambient only (docs/lighting.md §5.3 / §7).
    float3 color = g_Lighting.ambientRadiance * diffuseAlbedo * ao;

    if ((g_Lighting.flags & kLightingFlagDirectionalEnabled) != 0u)
    {
        const float3 L = normalize(g_Lighting.directional.toLight);
        color += EvaluateDirectBRDF(diffuseAlbedo, f0, alpha, roughness, N, V, L) *
                 g_Lighting.directional.color * g_Lighting.directional.intensity;
    }

    const uint pointCount = min(g_Lighting.pointLightCount, kMaxPointLights);
    [loop]
    for (uint i = 0u; i < pointCount; ++i)
    {
        const PointLightData light = g_Lighting.pointLights[i];
        const float3 toLight = light.position - worldPos;
        const float distance = length(toLight);
        const float att = PointLightAttenuation(distance, light.range);
        if (att <= 0.0)
        {
            continue;
        }

        // Spec §1: L = normalize(lightPosition - P). Attenuation floors d² separately.
        const float3 L = (distance > 0.0) ? (toLight / distance) : float3(0.0, 0.0, 0.0);
        color += EvaluateDirectBRDF(diffuseAlbedo, f0, alpha, roughness, N, V, L) *
                 light.color * light.intensity * att;
    }

    return float4(color, 1.0);
}
