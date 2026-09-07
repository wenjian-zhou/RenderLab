#include "postprocess.hlsli"

// S3.2 tone map to the back buffer or dump target (docs/postprocess.md).
// Reads scene-referred HDRSceneColor and writes display-referred linear values;
// the SRGBA8_UNORM target's hardware sRGB OETF is the pipeline's only output
// transfer. The curve math is frozen in postprocess.hlsli (S3.1).

ConstantBuffer<TonemapConstants> g_Tonemap : register(b0);

Texture2D t_HDRSceneColor : register(t0);

float4 main(float4 position : SV_Position) : SV_Target0
{
    const int2 pixelCoord = int2(position.xy);
    const float3 sceneColor = t_HDRSceneColor.Load(int3(pixelCoord, 0)).rgb;
    return float4(ApplyTonemapConstants(sceneColor, g_Tonemap), 1.0);
}
