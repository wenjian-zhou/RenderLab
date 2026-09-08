#ifndef RENDERLAB_RENDERER_CB_H
#define RENDERLAB_RENDERER_CB_H

// Shared C++ / HLSL constant-buffer contracts for RenderLab-owned passes.
// Do not include Donut GBufferFillPass or material_cb.h from renderer shaders.
//
// Alignment (D3D12 cbuffer / HLSL pack_matrix row_major):
//   - float4 and float4x4 start at 16-byte offsets
//   - float4x4 is 64 bytes, row-major, copied from donut::math::float4x4 with no transpose
//   - float3 + scalar share a 16-byte slot
//   - GPU CBV allocations are 256-byte aligned by NVRHI; these sizeof values are the payload
// Algebra: row-vector, p_out = mul(p_in, M). HLSL must keep #pragma pack_matrix(row_major).

#ifdef __cplusplus
#include <cstdint>
#include <donut/core/math/math.h>
namespace renderlab
{
using donut::math::float2;
using donut::math::float3;
using donut::math::float4;
using donut::math::float4x4;
using uint = std::uint32_t;
#else
#pragma pack_matrix(row_major)
#endif

static const uint RendererViewFlag_Mirrored = 1u; // det(worldToView.linear) < 0; frontCounterClockwise

static const uint MaterialFlag_HasBaseColorTexture  = 1u << 0;
static const uint MaterialFlag_HasMetalRoughTexture = 1u << 1;
static const uint MaterialFlag_HasNormalTexture     = 1u << 2;
static const uint MaterialFlag_HasOcclusionTexture  = 1u << 3;
static const uint MaterialFlag_TwoSided             = 1u << 4;

#ifdef __cplusplus
// C++-only typed aliases for the flag constants above. HLSL has no scoped
// enums and keeps using the prefixed names; the alias bits are identical
// (docs/code-style.md enum rules).
enum class RendererViewFlag : uint32_t
{
    None = 0,
    Mirrored = RendererViewFlag_Mirrored
};

enum class MaterialFlag : uint32_t
{
    None = 0,
    HasBaseColorTexture  = MaterialFlag_HasBaseColorTexture,
    HasMetalRoughTexture = MaterialFlag_HasMetalRoughTexture,
    HasNormalTexture     = MaterialFlag_HasNormalTexture,
    HasOcclusionTexture  = MaterialFlag_HasOcclusionTexture,
    TwoSided             = MaterialFlag_TwoSided
};

constexpr RendererViewFlag operator|(RendererViewFlag a, RendererViewFlag b)
{
    return static_cast<RendererViewFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr RendererViewFlag operator&(RendererViewFlag a, RendererViewFlag b)
{
    return static_cast<RendererViewFlag>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr MaterialFlag operator|(MaterialFlag a, MaterialFlag b)
{
    return static_cast<MaterialFlag>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

constexpr MaterialFlag operator&(MaterialFlag a, MaterialFlag b)
{
    return static_cast<MaterialFlag>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

constexpr RendererViewFlag& operator|=(RendererViewFlag& a, RendererViewFlag b)
{
    return a = a | b;
}

constexpr MaterialFlag& operator|=(MaterialFlag& a, MaterialFlag b)
{
    return a = a | b;
}
#endif

// sizeof = 16. Offset table: frameIndex @ 0.
struct FrameConstants
{
    uint  frameIndex;
    uint  pad0;
    uint  pad1;
    uint  pad2;
};

// sizeof = 448.
//   matWorldToView      @   0
//   matViewToClip       @  64
//   matWorldToClip      @ 128
//   matClipToView       @ 192
//   matViewToWorld      @ 256
//   matClipToWorld      @ 320
//   viewportOrigin      @ 384
//   viewportSize        @ 392
//   viewportSizeInv     @ 400
//   zNear               @ 408
//   flags               @ 412
//   cameraPosition      @ 416  xyz, w = 1
//   verticalFovRadians  @ 432
//   aspectRatio         @ 436
struct ViewConstants
{
    float4x4 matWorldToView;
    float4x4 matViewToClip;
    float4x4 matWorldToClip;
    float4x4 matClipToView;
    float4x4 matViewToWorld;
    float4x4 matClipToWorld;

    float2 viewportOrigin;
    float2 viewportSize;

    float2 viewportSizeInv;
    float  zNear;
    uint   flags;

    float4 cameraPosition;

    float  verticalFovRadians;
    float  aspectRatio;
    uint   pad0;
    uint   pad1;
};

// sizeof = 128.
//   matLocalToWorld @  0
//   matWorldToLocal @ 64
// Vertex shader: worldPos = mul(float4(localPos, 1), matLocalToWorld)
// World normal:  normalize(mul(localNormal, transpose((float3x3)matWorldToLocal)))
struct InstanceConstants
{
    float4x4 matLocalToWorld;
    float4x4 matWorldToLocal;
};

// sizeof = 32. Metal-rough factors; textures are bound separately.
//   baseColorFactor    @  0   linear Rec.709
//   roughness          @ 12   perceptual glTF roughness, not GGX alpha
//   metallic           @ 16
//   occlusionStrength  @ 20
//   normalScale        @ 24
//   flags              @ 28
// Shader evaluation (S1.4):
//   baseColor  = baseColorFactor * (HasBaseColorTexture ? tex.rgb : 1)
//   roughness  = roughness * (HasMetalRoughTexture ? orm.g : 1)
//   metallic   = metallic  * (HasMetalRoughTexture ? orm.b : 1)
//   ao         = lerp(1, HasOcclusionTexture ? occlusion.r : 1, occlusionStrength)
struct MaterialParams
{
    float3 baseColorFactor;
    float  roughness;

    float  metallic;
    float  occlusionStrength;
    float  normalScale;
    uint   flags;
};

#ifdef __cplusplus
} // namespace renderlab
#endif

#endif // RENDERLAB_RENDERER_CB_H
