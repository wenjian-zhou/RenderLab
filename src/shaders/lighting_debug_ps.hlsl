#include "renderer_cb.h"
#include "lighting_cb.h"
#include "lighting_debug_cb.h"
#include "lighting.hlsli"

// S2.2 lighting debug visualization to the back buffer or dump target.
// Encodings are not stored in HDRSceneColor (docs/lighting.md section 10).

ConstantBuffer<ViewConstants> g_View : register(b0);
ConstantBuffer<LightingConstants> g_Lighting : register(b1);
ConstantBuffer<LightingDebugConstants> g_Debug : register(b2);

Texture2D t_GBufferA : register(t0);
Texture2D t_GBufferB : register(t1);
Texture2D t_GBufferC : register(t2);
Texture2D t_GBufferDepth : register(t3); // R32_FLOAT SRV on the typeless D32 texture

float4 main(float4 position : SV_Position) : SV_Target0
{
    const float2 pixelPosition = position.xy;
    const int2 pixelCoord = int2(pixelPosition);

    const float4 targetB = t_GBufferB.Load(int3(pixelCoord, 0));
    const float deviceDepth = t_GBufferDepth.Load(int3(pixelCoord, 0)).r;
    const bool background = IsBackgroundDeviceDepth(deviceDepth);
    // Keep A/C in the shader so the frozen binding layout stays live.
    const float keepAlive =
        t_GBufferA.Load(int3(pixelCoord, 0)).a * 0.0 + t_GBufferC.Load(int3(pixelCoord, 0)).a * 0.0;

    if (g_Debug.mode == LightingDebugMode_WorldPosition)
    {
        if (background)
        {
            return float4(1.0, 0.0, 1.0, 1.0);
        }

        const float3 P = ReconstructWorldPosition(pixelPosition, deviceDepth, g_View);
        if (!(P.x == P.x) || !(P.y == P.y) || !(P.z == P.z) || isinf(P.x) || isinf(P.y) || isinf(P.z))
        {
            return float4(1.0, 0.0, 1.0, 1.0);
        }
        return float4(frac(abs(P)) + keepAlive, 1.0);
    }

    // LightingDebugMode_NdotL
    if (background)
    {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    const float3 N = normalize(targetB.rgb);
    const float3 L = normalize(g_Lighting.directional.toLight);
    const float ndotl = saturate(dot(N, L));
    return float4(ndotl.xxx + keepAlive, 1.0);
}
