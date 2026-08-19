#ifndef RENDERLAB_GBUFFER_PASS_HLSLI
#define RENDERLAB_GBUFFER_PASS_HLSLI

// Interpolators for the RenderLab opaque GBuffer pass.
// Consumes renderer_cb.h / DrawRecord streams, not Donut PlanarViewConstants.

struct GBufferVSInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD;
    float4 normal   : NORMAL;  // RGBA8_SNORM
    float4 tangent  : TANGENT; // RGBA8_SNORM, w = bitangent sign
};

struct GBufferVSOutput
{
    float4 clipPosition : SV_Position;
    float2 texCoord     : TEXCOORD;
    float3 worldNormal  : NORMAL;
    float4 worldTangent : TANGENT; // xyz world tangent, w = bitangent sign
};

#endif // RENDERLAB_GBUFFER_PASS_HLSLI
