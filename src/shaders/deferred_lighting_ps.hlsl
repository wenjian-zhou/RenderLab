#include "renderer_cb.h"
#include "lighting_cb.h"
#include "lighting.hlsli"

// S2.2 diagnostic deferred lighting. Writes scene-referred HDRSceneColor.
// Formula: saturate(N·L) * color * intensity for the directional light only.
// Ambient and point lights are intentionally ignored until S2.3.
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

    // A/C are bound for the frozen pass I/O contract; the S2.2 diagnostic only needs B + depth.
    const float4 targetB = t_GBufferB.Load(int3(pixelCoord, 0));
    const float deviceDepth = t_GBufferDepth.Load(int3(pixelCoord, 0)).r;
    // Keep A/C in the shader so the frozen binding layout stays live.
    const float keepAlive =
        t_GBufferA.Load(int3(pixelCoord, 0)).a * 0.0 + t_GBufferC.Load(int3(pixelCoord, 0)).a * 0.0;

    if (IsBackgroundDeviceDepth(deviceDepth))
    {
        return float4(g_Lighting.backgroundRadiance, 1.0);
    }

    // Prove reconstruction on the GPU path even though the diagnostic shades with N·L only.
    // Use the shared helper (docs/lighting.md §4); reject non-finite results as background.
    const float3 worldPos = ReconstructWorldPosition(pixelPosition, deviceDepth, g_View);
    if (!(worldPos.x == worldPos.x) || !(worldPos.y == worldPos.y) || !(worldPos.z == worldPos.z) ||
        isinf(worldPos.x) || isinf(worldPos.y) || isinf(worldPos.z))
    {
        return float4(g_Lighting.backgroundRadiance, 1.0);
    }
    (void)worldPos;

    const float3 N = normalize(targetB.rgb);
    float3 color = 0.0;
    if ((g_Lighting.flags & kLightingFlagDirectionalEnabled) != 0u)
    {
        const float3 L = normalize(g_Lighting.directional.toLight);
        const float ndotl = saturate(dot(N, L));
        color = ndotl * g_Lighting.directional.color * g_Lighting.directional.intensity;
    }

    return float4(color + keepAlive, 1.0);
}
