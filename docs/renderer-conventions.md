# Renderer Conventions

Status: **frozen for Stage 1**

Step: S1.1

This file is the coordinate, matrix, raster, and color-space contract for RenderLab-owned
passes. Shader and CPU code added in S1.2 and later must follow it. Donut/NVRHI owns the
device and swap chain; these conventions apply to RenderLab renderer data, not to a second
RHI.

If a later step needs a different convention, update this file and add an ADR. Do not invent
per-pass exceptions.

## 1. Spaces

| Space | Handedness | Axes | Origin / units |
|---|---|---|---|
| World | Right-handed | +X right, +Y up, +Z toward the glTF viewer | Scene units as authored (meters for the Cesium Milk Truck). Matches glTF 2.0 as loaded by Donut; RenderLab does not bake an extra axis swap. |
| View | Left-handed | +X camera right, +Y camera up, +Z camera forward | Camera origin. Produced by `donut::app::FirstPersonCamera::GetWorldToViewMatrix()`. |
| Clip / NDC | D3D | NDC X right, Y up. Device Z is reversed: `1` at the near plane, `0` at infinity. | After perspective divide: X,Y in `[-1, 1]`, Z in `[0, 1]`. |
| Window / texture | D3D | X right, Y down | Viewport and UV origin are top-left. |

World stays right-handed so glTF meshes, instance transforms, and later DXR instance matrices
share one space. View is left-handed because that is what Donut's first-person camera and
`perspProjD3DStyle*` matrices implement.

The world-to-view linear part is `affine3::from_cols(right, up, forward)` with
`right = normalize(cross(forward, up))`. Row-vector multiply `worldOffset * linear` therefore
projects onto camera right, up, and forward. That triad is left-handed in a right-handed
world, which is the intended conversion.

## 2. Matrix Convention

CPU math uses `donut::math`:

- Storage is **row-major**.
- Algebra is **row-vector**: `p_out = p_in * M`.
- Affine composition is `a * b` meaning apply `a` then `b`.
- A 4x4 view-projection matrix is `affineToHomogeneous(worldToView) * viewToClip`.

HLSL must match:

```hlsl
#pragma pack_matrix(row_major)

// Homogeneous round-trip. Do not replace clip.w with 1; that is NDC, not clip.
float4 clipPos = mul(float4(worldPos, 1.0), matWorldToClip);
float4 worldPosH = mul(clipPos, matClipToWorld);
float3 worldPos = worldPosH.xyz / worldPosH.w;
```

GBuffer lighting reconstructs from device depth and pixel UV using the formula in section 5,
not from a vertex-shader clip position.

Do not use `mul(M, v)` for these matrices. Constant-buffer `float4x4` members are 64 bytes,
16-byte aligned, and copied from `donut::math::float4x4` without transpose.

Required view-constant names (C++/HLSL structs live in
[`../src/shaders/renderer_cb.h`](../src/shaders/renderer_cb.h) and
[`renderer-data.md`](renderer-data.md)):

| Name | Meaning |
|---|---|
| `matWorldToView` | World to left-handed view |
| `matViewToClip` | View to clip, reversed-Z infinite projection |
| `matWorldToClip` | `matWorldToView * matViewToClip` |
| `matClipToView` | Inverse of `matViewToClip` |
| `matViewToWorld` | Inverse of `matWorldToView` |
| `matClipToWorld` | Inverse of `matWorldToClip` |

No temporal jitter. Pixel-offset / TAA matrices are out of scope before M4.

## 3. Front Face, Winding, and Culling

glTF 2.0 declares **counter-clockwise** triangles as front-facing in object/world space.

`FirstPersonCamera::GetWorldToViewMatrix()` always has `determinant(linear) < 0` because
view is left-handed in a right-handed world (`right = cross(forward, up)`). Donut treats
that as a mirrored view:

```text
PlanarView::m_IsMirrored = determinant(m_ViewMatrix.m_linear) < 0
GBufferFillPass frontCounterClockwise = view->IsMirrored()
```

The S0.4 camera is therefore mirrored in Donut's sense. D3D12 tests winding **on the render
target** (after the viewport Y-flip). Match Donut: counter-clockwise vertices on the render
target are front-facing.

Rasterizer and depth state for the opaque GBuffer pass:

| State | Value |
|---|---|
| `frontCounterClockwise` | `true` when `det(worldToView.linear) < 0` (always for `FirstPersonCamera`) |
| `cullMode` | `Back` for opaque single-sided materials |
| `cullMode` | `None` when the glTF material is `doubleSided` |
| `depthClipEnable` | `true` (override the NVRHI default of `false`) |
| `depthFunc` | `GreaterOrEqual` (reversed-Z; NVRHI default is `Less`) |
| `depthWriteEnable` | `true` |

Do not use the NVRHI/D3D default `frontCounterClockwise = false` with this camera. That
inverts culling relative to Donut and glTF. If a later view adds another reflection,
recompute from the determinant instead of hard-coding.

NVRHI also defaults `depthFunc` to `Less` and `depthClipEnable` to `false`. Both are wrong
for this projection. Donut's own GBuffer pass uses `GreaterOrEqual` when `IsReverseDepth()`
is true.

Pixel shaders that apply a normal map must still honor `SV_IsFrontFace`: if the fragment is
back-facing on a two-sided material, flip the world-space shading normal.

## 4. UV Origin and Texture Sampling

glTF `TEXCOORD_0` and D3D12 both use a **top-left** origin with V increasing downward. Do not
flip V when sampling Donut-loaded glTF textures.

Normal maps are tangent-space, linear UNORM, decoded as `n.xy = n.xy * 2 - 1` with +Z out of
the surface. Metallic-roughness and occlusion maps are linear. Base-color textures are sRGB;
see section 6.

Sampler state is not a GBuffer encoding concern. S1.4 uses linear wrap (repeat)
filtering and does not flip V.

## 5. Projection, NDC Depth, and Reversed-Z

RenderLab uses **reversed-Z infinite far** projection:

```text
proj = donut::math::perspProjD3DStyleReverse(verticalFovRadians, aspect, zNear)
```

The S0.4 camera preset supplies `verticalFovDegrees = 45` and `zNear = 0.1`. There is no
finite far plane. Aspect is `backBufferWidth / backBufferHeight`.

For this matrix, after perspective divide:

```text
deviceDepth = zNear / viewZ
```

| Location | `viewZ` | `deviceDepth` |
|---|---|---|
| Near plane | `zNear` | `1` |
| Infinity | `+inf` | `0` |
| Background (cleared) | n/a | `0` |

Depth test: `GreaterOrEqual`. Depth write: on for the opaque GBuffer pass. Hardware depth is
the authority; do not output `SV_Depth` unless a later pass has a documented reason.

The depth texture is `nvrhi::Format::D32`. NVRHI maps that to a `R32_TYPELESS` allocation,
a `D32_FLOAT` DSV, and an `R32_FLOAT` SRV. Lighting reconstructs from the SRV. See
[`g-buffer.md`](g-buffer.md).

Linearized view depth for debug (S1.5) is `viewZ = zNear / deviceDepth` and is rejected when
`deviceDepth` is 0.

Clip reconstruction for a texel or fullscreen pixel, matching Donut's
`ReconstructClipPosition` with a top-left UV:

```hlsl
float2 uv = (pixelPosition - viewportOrigin) * viewportSizeInv;
float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, deviceDepth, 1.0);
float4 worldPosH = mul(clipPos, matClipToWorld);
float3 worldPos = worldPosH.xyz / worldPosH.w;
```

- Fullscreen pixel shaders use `SV_Position.xy` (pixel centers).
- Compute or typed loads use `float2(pixel.xy) + 0.5`.
- `viewportOrigin` / `viewportSize` come from the NVRHI viewport, not from a second camera.

Background pixels must not be reconstructed. Treat `deviceDepth <= 0` as empty.

## 6. Color Space, HDR Units, and sRGB Boundaries

Working color space: **linear Rec.709 / sRGB primaries, D65**. This is the glTF 2.0 default
and the D3D12 swap-chain convention already used by the application
(`nvrhi::Format::SRGBA8_UNORM` back buffer).

| Stage | Encoding | Units |
|---|---|---|
| glTF `baseColorFactor` | Linear | Unitless reflectance, typically `[0, 1]` |
| glTF `baseColorTexture` | sRGB on disk; sampled linear | Same |
| GBufferA RGB | Linear values written to an sRGB RTV | Same. Hardware applies the sRGB OETF on store and the EOTF on an sRGB SRV load. |
| Metallic, roughness, AO, normals | Linear UNORM or float | See [`g-buffer.md`](g-buffer.md) |
| Deferred lighting output (S2) | Linear HDR | Scene-referred RGB in simplified scene units. See [`lighting.md`](lighting.md). Exposure-independent. |
| Tone map / present (S3) | Display-referred | The only output transfer is the hardware sRGB OETF on the `SRGBA8_UNORM` back buffer; the tone-map pass writes display-referred linear values. Curve, exposure, and UI composition are frozen in [`postprocess.md`](postprocess.md). UI composition stays on the sRGB back buffer. |
| ImGui | Donut ImGui path | Not a GBuffer consumer |

Rules:

1. There is exactly one linear-to-sRGB transfer in the pipeline: the hardware sRGB
   OETF on store to the `SRGBA8_UNORM` back buffer. The S3 tone-map pass writes
   display-referred linear values and performs no encoding of its own
   ([`postprocess.md`](postprocess.md) section 5).
2. GBuffer debug views (S1.5) apply a visualization encoding. That encoding is not the
   lighting path. Modes, remap, and linearized-depth display live in
   [`g-buffer.md`](g-buffer.md) section 10.
3. Do not write linear albedo into a non-sRGB 8-bit target. Do not write material flags into
   an sRGB target.
4. Stage 1 does not store HDR scene color. GBufferA is LDR reflectance. Values outside
   `[0, 1]` are not representable there and are not required by the committed scenes.
5. Dielectric F0 is **0.04** (glTF). It is derived in lighting from metallic and base color;
   it is not a GBuffer channel.
6. Roughness stored in the GBuffer is **perceptual** glTF roughness. Lighting squares it to
   GGX `alpha`. Do not pre-square in the GBuffer pass.

## 7. Camera

The locked S0.4 preset `s04-default` is the image-test camera:

- Position `(4.8, 2.4, 5.6)`, target `(0, 0.85, 0)`, up `(0, 1, 0)`
- Vertical FOV 45 degrees, `zNear` 0.1, infinite far, reversed-Z
- World meters, Y-up

Free-camera motion is allowed only when `--lock-camera` is off. It uses the same projection
and handedness. Image tests must lock the camera.

## 8. What This File Does Not Freeze

- Light intensity numeric defaults beyond the S2.1 scene-unit rule ([`lighting.md`](lighting.md))
- Post-process pass scheduling, exposure CLI, and the S3.2 LDR golden
  ([`postprocess.md`](postprocess.md))
- DXR ray space: world space, using the same instance transforms as raster (Stage 6)
- MSAA: sample count is 1 until a later ADR
- Velocity, TAA, emissive GBuffer, clustered lights, and material graphs (excluded before M4)
