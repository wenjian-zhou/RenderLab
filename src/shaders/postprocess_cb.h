#ifndef RENDERLAB_POSTPROCESS_CB_H
#define RENDERLAB_POSTPROCESS_CB_H

// Shared C++ / HLSL post-process constant-buffer contract (S3.1).
//
// The tone curve itself is frozen math, not constant-buffer state: the UE 5.8.1
// Filmic constants live in src/shaders/postprocess.hlsli and
// src/renderer/PostProcessContract.h, per docs/postprocess.md. The only runtime
// knob is exposure.
//
// Alignment matches renderer_cb.h:
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

// sizeof = 16.
//   exposureEV @  0   EV stops; linear scale = exp2(exposureEV), default 0 (x1).
//                  UE manual-exposure semantics: AutoExposureBias in EV stops
//                  (Scene.h:1928), multiplier 2^bias, no physical camera.
//   pad0       @  4
//   pad1       @  8
//   pad2       @ 12
struct TonemapConstants
{
    float exposureEV;
    uint  pad0;
    uint  pad1;
    uint  pad2;
};

#ifdef __cplusplus
} // namespace renderlab
#endif

#endif // RENDERLAB_POSTPROCESS_CB_H
