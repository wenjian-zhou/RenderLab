#include "renderer_cb.h"
#include "gbuffer_encoding.hlsli"
#include "gbuffer_pass.hlsli"

// Opaque GBuffer pixel shader. Writes GBufferA/B/C via EncodeGBuffer.
// Missing optional textures are 1x1 CPU fallbacks bound as SRVs; this does not invent a second BRDF.

ConstantBuffer<MaterialParams> g_Material : register(b3);

Texture2D t_BaseColor  : register(t0);
Texture2D t_MetalRough : register(t1);
Texture2D t_Normal     : register(t2);
Texture2D t_Occlusion  : register(t3);
SamplerState s_Material : register(s0);

GBufferPixelOut main(GBufferVSOutput input, bool isFrontFace : SV_IsFrontFace)
{
    const float2 uv = input.texCoord;

    const float3 baseColor = g_Material.baseColorFactor * t_BaseColor.Sample(s_Material, uv).rgb;

    const float4 orm = t_MetalRough.Sample(s_Material, uv);
    const float roughness = g_Material.roughness * orm.g;
    const float metallic = g_Material.metallic * orm.b;

    const float occlusion = t_Occlusion.Sample(s_Material, uv).r;
    const float ao = lerp(1.0, occlusion, g_Material.occlusionStrength);

    float3 geometryNormal = normalize(input.worldNormal);
    float3 tangent = input.worldTangent.xyz;
    const float tangentLengthSq = dot(tangent, tangent);
    if (tangentLengthSq > 1e-8)
    {
        tangent = tangent * rsqrt(tangentLengthSq);
        tangent = normalize(tangent - geometryNormal * dot(geometryNormal, tangent));
    }
    else
    {
        tangent = normalize(abs(geometryNormal.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0));
        tangent = normalize(tangent - geometryNormal * dot(geometryNormal, tangent));
    }
    const float3 bitangent = cross(geometryNormal, tangent) * input.worldTangent.w;

    float3 tangentNormal = t_Normal.Sample(s_Material, uv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= g_Material.normalScale;
    const float3x3 tbn = float3x3(tangent, bitangent, geometryNormal);
    float3 worldShadingNormal = normalize(mul(tangentNormal, tbn));

    const bool twoSided = (g_Material.flags & MaterialFlag_TwoSided) != 0u;
    if (twoSided && !isFrontFace)
    {
        worldShadingNormal = -worldShadingNormal;
    }

    uint flags = GBufferFlag_ShadingValid;
    if (twoSided)
    {
        flags |= GBufferFlag_TwoSided;
    }

    GBufferPixel pixel;
    pixel.baseColor = baseColor;
    pixel.worldNormal = worldShadingNormal;
    pixel.roughness = roughness;
    pixel.metallic = metallic;
    pixel.ao = ao;
    pixel.flags = flags;
    pixel.deviceDepth = 0.0;
    return EncodeGBuffer(pixel);
}
