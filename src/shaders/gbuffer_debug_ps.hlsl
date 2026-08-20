#include "renderer_cb.h"
#include "gbuffer_encoding.hlsli"
#include "gbuffer_debug_cb.h"

// GBuffer debug visualization. DecodeGBuffer is the only packing.
// Do not include Donut gbuffer_cb.h, PlanarViewConstants, or MaterialConstants.
// Visualization encoding is not the lighting path and is not the S3 tone map.

ConstantBuffer<ViewConstants> g_View : register(b0);
ConstantBuffer<GBufferDebugConstants> g_Debug : register(b1);

Texture2D t_GBufferA : register(t0);
Texture2D t_GBufferB : register(t1);
Texture2D t_GBufferC : register(t2);
Texture2D t_GBufferDepth : register(t3); // R32_FLOAT SRV on the S1.3 typeless D32 texture

float4 main(float4 position : SV_Position) : SV_Target0
{
    const int2 pixelCoord = int2(position.xy);
    const float4 targetA = t_GBufferA.Load(int3(pixelCoord, 0));
    const float4 targetB = t_GBufferB.Load(int3(pixelCoord, 0));
    const float4 targetC = t_GBufferC.Load(int3(pixelCoord, 0));
    const float deviceDepth = t_GBufferDepth.Load(int3(pixelCoord, 0)).r;

    const GBufferPixel gbuffer = DecodeGBuffer(targetA, targetB, targetC, deviceDepth);
    const bool background = deviceDepth <= 0.0;

    float3 color = 0.0;

    if (g_Debug.mode == GBufferDebugMode_BaseColor)
    {
        // Linear reflectance from the sRGB GBufferA SRV. Hardware OETF on the sRGB back buffer.
        color = gbuffer.baseColor;
    }
    else if (g_Debug.mode == GBufferDebugMode_WorldNormal)
    {
        if (background)
        {
            // Cleared normal is (0,0,0). Do not remap it to gray.
            color = 0.0;
        }
        else
        {
            color = gbuffer.worldNormal * 0.5 + 0.5;
        }
    }
    else if (g_Debug.mode == GBufferDebugMode_Roughness)
    {
        color = gbuffer.roughness.xxx;
    }
    else if (g_Debug.mode == GBufferDebugMode_Metallic)
    {
        color = gbuffer.metallic.xxx;
    }
    else if (g_Debug.mode == GBufferDebugMode_AoFlags)
    {
        const float shadingValid = ((gbuffer.flags & GBufferFlag_ShadingValid) != 0u) ? 1.0 : 0.0;
        const float twoSided = ((gbuffer.flags & GBufferFlag_TwoSided) != 0u) ? 1.0 : 0.0;
        color = float3(gbuffer.ao, shadingValid, twoSided);
    }
    else if (g_Debug.mode == GBufferDebugMode_LinearDepth)
    {
        if (background)
        {
            // deviceDepth == 0 is cleared / infinite far. Do not reconstruct.
            color = float3(1.0, 0.0, 1.0);
        }
        else
        {
            const float viewZ = g_View.zNear / deviceDepth;
            const float displayed = viewZ / (viewZ + 1.0);
            color = displayed.xxx;
        }
    }

    return float4(color, 1.0);
}
