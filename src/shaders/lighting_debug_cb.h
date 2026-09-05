#ifndef RENDERLAB_LIGHTING_DEBUG_CB_H
#define RENDERLAB_LIGHTING_DEBUG_CB_H

// Debug-visualization constants for lighting views. Does not change lighting_cb.h.
// World-position / N·L / lit encodings are not stored in HDRSceneColor.

#ifdef __cplusplus
#include <cstdint>
namespace renderlab
{
using uint = std::uint32_t;
#else
#pragma pack_matrix(row_major)
#endif

static const uint LightingDebugMode_WorldPosition = 0;
static const uint LightingDebugMode_NdotL = 1;
static const uint LightingDebugMode_Lit = 2;
static const uint LightingDebugMode_Count = 3;

// sizeof = 16.
struct LightingDebugConstants
{
    uint mode;
    uint pad0;
    uint pad1;
    uint pad2;
};

#ifdef __cplusplus
enum class LightingDebugMode : uint32_t
{
    WorldPosition = LightingDebugMode_WorldPosition,
    NdotL = LightingDebugMode_NdotL,
    Lit = LightingDebugMode_Lit,
    Count = LightingDebugMode_Count
};
} // namespace renderlab
#endif

#endif // RENDERLAB_LIGHTING_DEBUG_CB_H
