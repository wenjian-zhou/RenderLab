#ifndef RENDERLAB_LIGHTING_HLSLI
#define RENDERLAB_LIGHTING_HLSLI

#include "renderer_cb.h"

// Shared lighting helpers. Matches src/renderer/LightingContract.h and docs/lighting.md.
// Callers must reject background pixels (deviceDepth <= 0) before reconstructing.

bool IsBackgroundDeviceDepth(float deviceDepth)
{
    return deviceDepth <= 0.0;
}

float3 ReconstructWorldPosition(float2 pixelPosition, float deviceDepth, ViewConstants view)
{
    const float2 uv = (pixelPosition - view.viewportOrigin) * view.viewportSizeInv;
    const float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, deviceDepth, 1.0);
    const float4 worldPosH = mul(clipPos, view.matClipToWorld);
    return worldPosH.xyz / worldPosH.w;
}

#endif // RENDERLAB_LIGHTING_HLSLI
