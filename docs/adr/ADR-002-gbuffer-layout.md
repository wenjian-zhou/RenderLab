# ADR-002: GBuffer Layout

- Status: Accepted
- Date: 2026-08-19
- Step: S1.1
- Related: [`../g-buffer.md`](../g-buffer.md), [`../renderer-conventions.md`](../renderer-conventions.md)

## Decision

The first RenderLab GBuffer is four resources and no more:

| Debug name | NVRHI format | Channels |
|---|---|---|
| `GBufferA` | `SRGBA8_UNORM` | Linear base color RGB, A unused (`1`) |
| `GBufferB` | `RGBA16_FLOAT` | World-space unit shading normal RGB, perceptual roughness A |
| `GBufferC` | `RGBA8_UNORM` | Metallic R, AO G, packed material flags B, A unused (`1`) |
| `GBufferDepth` | `D32` (typeless) | Reversed-Z device depth; DSV `D32_FLOAT`, SRV `R32_FLOAT` |

Velocity is omitted. Sample count is 1. Clear values and background rules are in
[`../g-buffer.md`](../g-buffer.md).

RenderLab implements this layout in its own GBuffer pass. Donut `GBufferFillPass` remains
unused, matching ADR-001.

## Context

`IMPLEMENTATION_PLAN.md` S1.1 requires the minimum deferred-lighting targets: base color,
world normal plus roughness, metallic/AO/material flags, and depth. `NEW_PLAN.md` suggested
the same four resources and listed velocity as optional. Stage 2 lighting needs those
channels and a reconstructable depth; it does not need motion vectors, emissive, or a
pre-baked F0 target.

The application already presents an `SRGBA8_UNORM` back buffer through Donut. glTF 2.0
metallic-roughness is the material model of both committed scenes.

## Options Considered

### 1. Adopt Donut's four-color GBuffer

Donut `GBufferRenderTargets` stores diffuse sRGB, specular sRGB, `RGBA16_SNORM` normals, HDR
emissive, and optional `RG16_FLOAT` motion vectors.

Rejected. ADR-001 places Donut's GBuffer/deferred passes outside the learning goal. Donut
also stores BRDF-ready diffuse/F0 rather than metal-rough, which would hide the conversion
RenderLab needs to own in S1.2 / S2.

### 2. `RGBA8_UNORM` base color instead of `SRGBA8_UNORM`

Rejected. Linear 8-bit albedo bands in dark materials. Hardware sRGB on `GBufferA` is the
correct 8-bit store for LDR reflectance. Material flags stay off this target so sRGB does
not corrupt bits.

### 3. `RGBA16_FLOAT` base color

Rejected for v1. The committed scenes are LDR. Doubling albedo bandwidth does not buy a
lighting term Stage 2 needs.

### 4. `RGBA16_SNORM` normals (Donut) or octahedral `RG16`

`RGBA16_SNORM` can store a unit normal, but roughness in alpha is `[0, 1]` and would sit in
the unsigned half of a signed format. Octahedral packing saves bandwidth at the cost of
encode/decode bugs in every debug and lighting path.

Selected: `RGBA16_FLOAT` as `NEW_PLAN.md` suggested. No packing, roughness is a plain scalar,
and the RTX 4070 SUPER supports it as RTV and SRV.

### 5. Pack metallic into `GBufferA.a` and drop `GBufferC`

Rejected. `IMPLEMENTATION_PLAN.md` names metallic/AO/flags as their own target. Packing
flags into an sRGB alpha channel is unsafe.

### 6. `D24S8` or `D32S8` depth

Rejected. Stage 1 has no stencil consumer. Reversed-Z wants 32-bit floating depth.
`D32` already exposes an `R32_FLOAT` SRV through NVRHI, which S2.2 reconstruction needs.

### 7. Include velocity now

Rejected. The plan forbids adding velocity before a real consumer. TAA is excluded before M4.

## Consequences

- S1.3 can create four named textures without choosing formats again.
- S1.4 writes three color MRTs plus depth. The pixel-shader `SV_Target` mapping is fixed.
- S1.5 debug modes are exactly: base color, world normal, roughness, metallic, AO/flags,
  linearized depth.
- S2 lighting derives F0 and diffuse albedo from base color and metallic. It reconstructs
  position from `GBufferDepth` and `matClipToWorld`.
- Adding a fifth target later requires a new ADR and a GBuffer pass change. That is
  intentional.
- AMD RDNA validation is not part of S1.1. The chosen formats are Feature Level 11_0
  required typed 2D views; a later adapter still has to run
  `scripts/check-gbuffer-formats.cpp`.

## Validation Performed In S1.1

- Read Donut `GBuffer.cpp`, `gbuffer.hlsli`, `perspProjD3DStyleReverse`,
  `FirstPersonCamera::UpdateWorldToView`, and NVRHI `dxgi-format.cpp` at the pinned
  revisions.
- Queried D3D12 format support on NVIDIA GeForce RTX 4070 SUPER (feature level 12_2):
  `SRGBA8` / `RGBA16_FLOAT` / `RGBA8` RTV+SRV, `D32_FLOAT` DSV, `R32_FLOAT` SRV, and a
  typeless depth resource that accepts both views.
- Confirmed each planned S2 lighting input has a single documented source in
  [`../g-buffer.md`](../g-buffer.md).
