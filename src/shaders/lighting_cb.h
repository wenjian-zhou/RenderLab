#ifndef RENDERLAB_LIGHTING_CB_H
#define RENDERLAB_LIGHTING_CB_H

// Shared C++ / HLSL lighting constant-buffer contract (S2.1).
// Do not include Donut deferred_lighting_cb.h from renderer shaders.
// View matrices stay in renderer_cb.h ViewConstants. Do not duplicate them here.
//
// Alignment matches renderer_cb.h:
//   - float4 and float4x4 start at 16-byte offsets
//   - float3 + scalar share a 16-byte slot
//   - GPU CBV allocations are 256-byte aligned by NVRHI; these sizeof values are the payload

#ifdef __cplusplus
#include <cstdint>
#include <donut/core/math/math.h>
namespace renderlab
{
using donut::math::float3;
using uint = std::uint32_t;
#else
#pragma pack_matrix(row_major)
#endif

static const uint kMaxPointLights = 8;
static const uint kLightingFlagDirectionalEnabled = 1u;

// sizeof = 32.
//   toLight    @  0   world unit, surface-to-light (no hidden negate)
//   intensity  @ 12   scene units; radiance = color * intensity
//   color      @ 16   linear Rec.709
//   pad0       @ 28
struct DirectionalLightData
{
    float3 toLight;
    float  intensity;

    float3 color;
    float  pad0;
};

// sizeof = 32.
//   position   @  0   world, scene units
//   range      @ 12   world units; att = 0 when d >= range
//   color      @ 16   linear Rec.709
//   intensity  @ 28   scene units before attenuation
struct PointLightData
{
    float3 position;
    float  range;

    float3 color;
    float  intensity;
};

// sizeof = 320.
//   directional         @   0
//   ambientRadiance     @  32
//   pointLightCount     @  44   clamped to kMaxPointLights
//   backgroundRadiance  @  48
//   flags               @  60   bit 0 = directional enabled
//   pointLights[8]      @  64
struct LightingConstants
{
    DirectionalLightData directional;

    float3 ambientRadiance;
    uint   pointLightCount;

    float3 backgroundRadiance;
    uint   flags;

    PointLightData pointLights[kMaxPointLights];
};

#ifdef __cplusplus
} // namespace renderlab
#endif

#endif // RENDERLAB_LIGHTING_CB_H
