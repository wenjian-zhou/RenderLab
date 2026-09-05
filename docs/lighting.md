# Lighting Contract

Status: **S2.1 frozen; S2.3 implemented**

Step: S2.1 (contract) / S2.2 (HDR + diagnostic) / S2.3 (BRDF + lit debug)

This file is the first-version deferred lighting contract. It is a deliberately
small physically based scope: a reference workload for later Mini RDG migration
and an optional DXR visibility input. It is not a production deferred renderer.

Coordinate, matrix, reversed-Z, and color-space rules live in
[`renderer-conventions.md`](renderer-conventions.md). GBuffer encodings live in
[`g-buffer.md`](g-buffer.md). This file only describes lighting spaces, the HDR
target, lights, the first BRDF, pass I/O, debug views, and validation.

S2.1 froze the shader interface. S2.2 created `HDRSceneColor` and proved
reconstruction with a directional `N·L` diagnostic. S2.3 evaluates Lambert +
UE DefaultLit GGX into `HDRSceneColor`, presents lighting debug views
(`world-position` / `ndotl` / `lit`) by default as `lit` (Reinhard of HDR), and
supports `--verify-lights` for a fixed point + ambient fixture. Do not add
clustered lighting, IBL, shadows, or a second material model.

## 1. Spaces

All shading vectors are **world space**.

| Vector | Space | Source |
|---|---|---|
| `N` | World | `normalize(GBufferB.rgb)` |
| `P` | World | `ReconstructWorldPosition` from `GBufferDepth` + `ViewConstants` |
| `V` | World | `normalize(cameraPosition.xyz - P)` |
| `L` (directional) | World | `normalize(toLight)` |
| `L` (point) | World | `normalize(lightPosition - P)` |
| `H` | World | `normalize(V + L)` |

View space is only an intermediate while reconstructing from reversed-Z device
depth. Do not transform GBuffer normals into view space.

Do not re-flip two-sided normals. The GBuffer pass already honors `SV_IsFrontFace`.
`TwoSided` is informational.

## 2. `HDRSceneColor`

One persistent size-dependent texture. The lighting pass does not own it.

| Field | Value |
|---|---|
| Debug name | `HDRSceneColor` |
| NVRHI format | `RGBA16_FLOAT` |
| Color space | Linear Rec.709 / sRGB primaries, D65, scene-referred |
| Units | Simplified scene units. Exposure-independent. S3 is the only output transfer. |
| Alpha | Unused. Write `1`. |
| Clear | `(0, 0, 0, 1)` |
| Usage | Render target + shader resource. No UAV. |
| Initial state | `RenderTarget`, `keepInitialState = true` |
| Size | Back-buffer width × height, sample count 1, mip count 1. 2D, depth 1, array size 1, sample quality 0. Zero width/height is rejected when the texture is created (S2.2), not by the descriptor matcher, matching GBuffer. |
| Bytes | 8 B/px; 7,372,800 B at 1280×720 |
| Resize | Same Donut sequence as GBuffer: `BackBufferResizing` → release, `BackBufferResized` → create. Minimize does not destroy it. |

S2.2 creates the texture from `MakeHDRSceneColorTextureDesc` via
`HDRSceneColorTarget` (app-owned; same resize sequence as GBuffer).

Do not store visualization encodings in `HDRSceneColor`. It is always
scene-referred lighting from the deferred BRDF (or the historical S2.2 diagnostic).

## 3. Background

A pixel is background if and only if `deviceDepth <= 0`. That compare is exact
for a cleared `D32` target. If `ShadingValid` disagrees, **depth still wins**.

On background pixels:

- Do not reconstruct.
- Do not evaluate a BRDF.
- Do not treat cleared `GBufferA = (0,0,0)` as a black dielectric.
- Write `float4(backgroundRadiance, 1)` with default `backgroundRadiance = (0, 0, 0)`.

Debug views may use magenta for “not reconstructed.” `HDRSceneColor` must not.

## 4. World-position reconstruction

Callers reject `deviceDepth <= 0`, then call the shared helper. Do not copy the
formula into each shader.

```hlsl
float3 ReconstructWorldPosition(float2 pixelPosition, float deviceDepth, ViewConstants view)
{
    float2 uv = (pixelPosition - view.viewportOrigin) * view.viewportSizeInv;
    float4 clipPos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, deviceDepth, 1.0);
    float4 worldPosH = mul(clipPos, view.matClipToWorld);
    return worldPosH.xyz / worldPosH.w;
}
```

This is NDC reconstruction (`clip.w = 1` on the constructed point), matching
[`renderer-conventions.md`](renderer-conventions.md) section 5. Fullscreen pixel
shaders pass `SV_Position.xy`. CPU and HLSL must match
[`../src/shaders/lighting.hlsli`](../src/shaders/lighting.hlsli) and
[`../src/renderer/LightingContract.h`](../src/renderer/LightingContract.h).

If `worldPosH.w` is 0 or non-finite after a valid depth, treat the pixel as
invalid and write `backgroundRadiance`.

## 5. Lights

Lights are RenderLab-owned CPU data. Do not parse `KHR_lights_punctual`.
Intensities are **simplified scene units**, not lux or candela.

### 5.1 Directional

`toLight` is a unit **surface-to-light** vector. `(0, 1, 0)` lights the surface
from +Y. The shader uses `L = normalize(toLight)` with no hidden negate.

```text
radiance = color * intensity
att      = 1
```

Default fill (uploaded every frame until a UI exists):

```text
toLight   = normalize(0.45, 0.80, 0.40)
color     = (1, 1, 1)
intensity = 4
flags bit 0 = 1 (directional enabled)
```

### 5.2 Point lights

Maximum **8**, stored inline in `LightingConstants`. `pointLightCount` is clamped
to 8. A 9th light is dropped on the CPU with a diagnostic (warning at most once
per process); the shader never walks past the array. Default count is 0.
`--verify-lights` uploads one fixture point light (see §10).

| Field | Meaning |
|---|---|
| `position` | World space, scene units |
| `color` | Linear Rec.709 |
| `intensity` | Scene units before attenuation |
| `range` | World units. Cutoff only. |

```text
d   = length(lightPosition - P)
att = 0                         if d >= range or range <= 0
att = 1 / max(d * d, 1e-4)      otherwise
```

Same intensity and same `d` therefore match for any range that still contains `d`.
`range` does not change in-range brightness. There may be a small pop at the
boundary; that is accepted.

### 5.3 Ambient

One RGB ambient radiance. Default `(0, 0, 0)`. No hemisphere, SH, or IBL.

```text
L_ambient = ambientRadiance * diffuseAlbedo * ao
```

## 6. First BRDF

Separate Lambert and microfacet GGX. Not Disney / principled.

```text
baseColor      = GBufferA.rgb
N              = normalize(GBufferB.rgb)
roughness      = GBufferB.a
metallic       = GBufferC.r
ao             = GBufferC.g

diffuseAlbedo  = baseColor * (1 - metallic)
fd             = diffuseAlbedo / π
f0             = lerp(0.04, baseColor, metallic)
alpha          = max(roughness * roughness, 1e-3)
```

Dielectric F0 `0.04` is already frozen in the renderer conventions. Do not store
F0 in the GBuffer. The `1e-3` floor is on `alpha`, not on stored roughness.

Specular is UE DefaultLit `D_GGX * Vis_SmithJointApprox * F_Schlick`, with
diffuse and specular evaluated as separate lobes (not a single Fresnel-lerped
`fr`). `Vis` already includes `G / (4 N·L N·V)`.

```text
a2  = alpha * alpha
D   = a2 / (π * (NdotH² * (a2 - 1) + 1)²)
a   = alpha
Vis = 0.5 / (NdotL * (NdotV * (1 - a) + a) + NdotV * (NdotL * (1 - a) + a))
F   = saturate(50 * f0.g) * (1 - VdotH)⁵ + (1 - (1 - VdotH)⁵) * f0
      (UE BRDF.ush F_Schlick; <2% F0.g treated as shadowing)
fd  = diffuseAlbedo / π
fs  = D * Vis * F
```

`NdotV` uses `saturate(abs(N·V) + 1e-5)` (UE DefaultLit). Saturate `NdotL`,
`NdotH`, `VdotH`. Use `ε = 1e-5` in any extra denominator defense. Skip the light
when `NdotL <= 0`.

Analytic multi-scatter energy terms match UE
`USE_ENERGY_CONSERVATION == 2` (no LUT texture):

```text
(E, Ef) = GGXEnergyLookupAnalytic(roughness, NdotV)
F90     = saturate(50 * max3(f0))
W       = 1 + f0 * ((1 - E) / E)
Erefl   = W * (E * f0 + Ef * (F90 - f0))
fd'     = fd * saturate(1 - LuminanceRec709(Erefl))
fs'     = fs * W
Lo_k    = (fd' + fs') * (color * intensity * att) * saturate(NdotL) * Vk
```

Directional lights use `att = 1`. `Vk = 1` in Stage 2. S7.4 multiplies **direct**
terms only. Ambient stays `ambientRadiance * diffuseAlbedo * ao` (no energy terms).

Point `L` is `normalize(lightPosition - P)` when `d > 0`; attenuation still floors
`d²` at `1e-4` independently.

## 7. AO

`GBufferC.g` multiplies **ambient only**. Direct directional and point lights are
not multiplied by AO. If ambient is 0, AO does not change the lit image. The
GBuffer `ao-flags` debug view still shows the channel.

## 8. Constant buffers

Reuse `ViewConstants` / `FrameConstants`. Do not duplicate matrices.

Shared C++/HLSL payload: [`../src/shaders/lighting_cb.h`](../src/shaders/lighting_cb.h).
Debug mode: [`../src/shaders/lighting_debug_cb.h`](../src/shaders/lighting_debug_cb.h).
CPU helpers and HDR descriptor: [`../src/renderer/LightingContract.h`](../src/renderer/LightingContract.h).

`LightingConstants` is **320 bytes**, 16-byte aligned, copied once per frame.
NVRHI CBV allocations stay 256-byte aligned at bind time; that is not `sizeof`.

| Offset | Field |
|---|---|
| 0 | `directional.toLight` |
| 12 | `directional.intensity` |
| 16 | `directional.color` |
| 28 | `directional.pad0` |
| 32 | `ambientRadiance` |
| 44 | `pointLightCount` |
| 48 | `backgroundRadiance` |
| 60 | `flags` (bit 0 = directional enabled) |
| 64 | `pointLights[8]` (8 × 32 bytes) |

Each point light is `position.xyz`, `range`, `color.xyz`, `intensity`.

`LightingDebugConstants` is 16 bytes: `mode` plus padding. Do not mix it into
`LightingConstants`.

## 9. Pass parameters

Named for later RDG migration. Caller owns texture lifetime.

**Inputs**

- `gbufferA`, `gbufferB`, `gbufferC` (color SRVs)
- `gbufferDepth` (existing `R32_FLOAT` SRV; not rebound as a DSV)
- `viewConstants`
- `lightingConstants`

**Output**

- `hdrSceneColor` (RTV)

| Resource | Later RDG access |
|---|---|
| `gbufferA/B/C` | Shader resource read |
| `gbufferDepth` | Shader resource read |
| `hdrSceneColor` | Render target write |

GPU marker: `DeferredLighting`, nested under `Render` after `GBuffer`. Timestamp
the same scope. Fullscreen triangle. Depth test/write off. Cull none. Loop one
directional plus `pointLightCount` points in the pixel shader.

S7.4 reserved name, **not** an S2 field: `directVisibility`. Missing / disabled
means 1. It will multiply direct light only.

## 10. Debug views and verification fixture

Visualization, presented like GBuffer debug (`debugColor` = back buffer or dump).
Not the meaning of `HDRSceneColor`.

CLI: `--lighting-view world-position|ndotl|lit`. Mutually exclusive with
`--gbuffer-view`. With no view flags, present defaults to lighting `lit`.
Dump with `--dump-lighting-views <dir>` (orthogonal to `--dump-gbuffer-views`;
not part of S1.6 `--output` / golden).

| CLI name | Channel | Encoding | Background |
|---|---|---|---|
| `world-position` | Reconstructed `P` | `frac(abs(P))` | Magenta `(1, 0, 1)` |
| `ndotl` | `saturate(dot(N, toLight))` | Grayscale | Black `(0, 0, 0)` |
| `lit` | `HDRSceneColor.rgb` | Reinhard `hdr/(1+hdr)` | Reinhard of `backgroundRadiance` |

`ndotl` uses the same directional `toLight` as lighting. `lit` **reads**
`HDRSceneColor`; it does not re-evaluate the BRDF and does not write visualization
encodings back into HDR.

S2.3 `DeferredLighting` writes Lambert + GGX into `HDRSceneColor` (background
writes `backgroundRadiance`). GPU marker nesting under `Render`: `GBuffer`, then
`DeferredLighting` (timestamped), then exactly one of `GBufferDebug` or
`LightingDebug`.

### `--verify-lights` fixture

Default fill is unchanged (directional only, `pointLightCount = 0`, ambient 0).
With `--verify-lights`, upload:

| Field | Value |
|---|---|
| Directional | Contract default (`toLight`, color, intensity 4, enabled) |
| Point[0] | `position=(0,2,0)`, `color=(1,0.9,0.8)`, `intensity=20`, `range=8` |
| `pointLightCount` | 1 |
| Ambient | `(0.03, 0.03, 0.035)` |

Metal / roughness response checklist: `--scene fallback-boxes --lighting-view lit`
(and optionally `--verify-lights`). Default scene remains `cesium-milk-truck`.

## 11. Explicit exclusions

- Shadows, cascaded maps, contact shadows
- DXR / a visibility texture as an S2 input
- Emissive
- IBL, SH, env maps, hemisphere ambient
- Clustered, tiled, or forward+ light lists
- Spot, area, IES, cookies
- Automatic exposure
- TAA, velocity, bloom, color grading
- Clearcoat, sheen, anisotropy, transmission, SSS, spec-gloss
- MSAA
- Compute lighting / UAV HDR
- glTF punctual-light import
- Photometric units
- Multi-scatter LUT / energy-compensation **textures** (analytic UE fit is used)
- Disney diffuse, height-correlated Smith (beyond `Vis_SmithJointApprox`), Karis `G1`

## 12. Validation

| Case | Expected |
|---|---|
| `deviceDepth <= 0` | No reconstruct, no BRDF, HDR = `backgroundRadiance` |
| Depth vs `ShadingValid` disagree | Depth wins |
| Zero lights and zero ambient | Geometry black, background unchanged |
| `roughness = 0` | `alpha = 1e-3`, finite specular |
| Grazing (`NdotV` ~ 0) | Finite |
| Point light at `d = 0` | Finite (`1 / 1e-4`) |
| `d >= range` | That light is 0 |
| Same `d`, different ranges still containing `d` | Same attenuation |
| `pointLightCount > 8` | CPU clamp + diagnostic |
| Camera moves | `frac(abs(P))` stays planted on the mesh |
| Resize | `HDRSceneColor` recreated with GBuffer |
| NaN / Inf | Fail the later S2.4 capture |

S2.1 CPU tests: struct sizes and offsets, F0 lerp, alpha floor, attenuation
endpoints, count clamp, NDC→world reconstruction with the S0.4 camera, HDR
descriptor flags.

S2.2 GPU: reconstruction stability and `N·L` (historical). S2.3 GPU: BRDF into
HDR + `lit` Reinhard present. S2.3 CPU: lighting debug CLI (`lit`), default
present=`lit`, `--verify-lights` fixture constants, deferred/lighting-debug raster
state. S2.4 is HDR regression and finite-pixel checks.
