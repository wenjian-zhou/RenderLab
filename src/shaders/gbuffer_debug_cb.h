#ifndef RENDERLAB_GBUFFER_DEBUG_CB_H
#define RENDERLAB_GBUFFER_DEBUG_CB_H

// Debug-visualization constants for S1.5. Does not change renderer_cb.h.
// Shaders decode GBuffer via gbuffer_encoding.hlsli and read ViewConstants.zNear.

#ifdef __cplusplus
#include <cstdint>
namespace renderlab
{
using uint = std::uint32_t;
#else
#pragma pack_matrix(row_major)
#endif

// ADR-002 debug modes. Values are the GPU constant-buffer payload.
static const uint GBufferDebugMode_BaseColor = 0;
static const uint GBufferDebugMode_WorldNormal = 1;
static const uint GBufferDebugMode_Roughness = 2;
static const uint GBufferDebugMode_Metallic = 3;
static const uint GBufferDebugMode_AoFlags = 4;
static const uint GBufferDebugMode_LinearDepth = 5;
static const uint GBufferDebugMode_Count = 6;

// sizeof = 16.
struct GBufferDebugConstants
{
    uint mode;
    uint pad0;
    uint pad1;
    uint pad2;
};

#ifdef __cplusplus
enum class GBufferDebugMode : uint32_t
{
    BaseColor = GBufferDebugMode_BaseColor,
    WorldNormal = GBufferDebugMode_WorldNormal,
    Roughness = GBufferDebugMode_Roughness,
    Metallic = GBufferDebugMode_Metallic,
    AoFlags = GBufferDebugMode_AoFlags,
    LinearDepth = GBufferDebugMode_LinearDepth,
    Count = GBufferDebugMode_Count
};
} // namespace renderlab
#endif

#endif // RENDERLAB_GBUFFER_DEBUG_CB_H
