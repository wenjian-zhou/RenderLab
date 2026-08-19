#ifndef RENDERLAB_GBUFFER_ENCODING_HLSLI
#define RENDERLAB_GBUFFER_ENCODING_HLSLI

// Matches src/renderer/GBufferContract.h and docs/g-buffer.md.
// GBuffer write is S1.4; this header is the frozen encode/decode contract.

static const uint GBufferFlag_ShadingValid = 1u << 0;
static const uint GBufferFlag_TwoSided     = 1u << 1;
static const uint GBufferFlag_AlphaTested  = 1u << 2;

float PackGBufferFlags(uint flags)
{
    return flags / 255.0;
}

uint UnpackGBufferFlags(float packed)
{
    return uint(packed * 255.0 + 0.5);
}

struct GBufferPixelOut
{
    float4 targetA : SV_Target0; // rgb linear base color, a = 1
    float4 targetB : SV_Target1; // rgb world normal, a perceptual roughness
    float4 targetC : SV_Target2; // r metallic, g AO, b packed flags, a = 1
};

struct GBufferPixel
{
    float3 baseColor;
    float3 worldNormal;
    float  roughness;
    float  metallic;
    float  ao;
    uint   flags;
    float  deviceDepth;
};

GBufferPixelOut EncodeGBuffer(GBufferPixel pixel)
{
    GBufferPixelOut outValue;
    outValue.targetA = float4(pixel.baseColor, 1.0);
    outValue.targetB = float4(pixel.worldNormal, pixel.roughness);
    outValue.targetC = float4(pixel.metallic, pixel.ao, PackGBufferFlags(pixel.flags), 1.0);
    return outValue;
}

GBufferPixel DecodeGBuffer(float4 a, float4 b, float4 c, float deviceDepth)
{
    GBufferPixel pixel;
    pixel.baseColor = a.rgb;
    pixel.worldNormal = b.xyz;
    pixel.roughness = b.a;
    pixel.metallic = c.r;
    pixel.ao = c.g;
    pixel.flags = UnpackGBufferFlags(c.b);
    pixel.deviceDepth = deviceDepth;
    return pixel;
}

#endif // RENDERLAB_GBUFFER_ENCODING_HLSLI
