# GBuffer Contract

Status: **frozen for Stage 1**

Step: S1.1

This file is the first-version GBuffer layout. S1.3 creates the textures. S1.4 writes them.
S1.5 visualizes them. S2 lighting reads them. Do not add targets, channels, or encodings
without updating this file and [`adr/ADR-002-gbuffer-layout.md`](adr/ADR-002-gbuffer-layout.md).

Coordinate, matrix, reversed-Z, and color-space rules live in
[`renderer-conventions.md`](renderer-conventions.md). This file only describes the GBuffer
resources and how later passes consume them.

Donut's `GBufferFillPass` is not used. RenderLab owns the pass and the packing.

## 1. Target Set

Four resources, sample count 1, no mip chain, size = back-buffer width × height:

| Resource debug name | MRT / binding | NVRHI format | Views | Contents |
|---|---|---|---|---|
| `GBufferA` | `SV_Target0` | `SRGBA8_UNORM` | RTV + SRV | Linear base color in RGB. A unused. |
| `GBufferB` | `SV_Target1` | `RGBA16_FLOAT` | RTV + SRV | World-space unit shading normal in RGB. Perceptual roughness in A. |
| `GBufferC` | `SV_Target2` | `RGBA8_UNORM` | RTV + SRV | Metallic, AO, packed material flags. A unused. |
| `GBufferDepth` | depth attachment | `D32` | DSV + SRV | Reversed-Z device depth. |

There is **no velocity target**. TAA and motion vectors are excluded before M4. Do not add
them until a real consumer exists.

There is no emissive, specular-F0, or view-space-normal target. Those quantities are either
derived in lighting or out of scope for the first deferred pass.

Approximate allocation at 1280×720, no MSAA:

| Resource | Bytes/pixel | Bytes at 1280×720 |
|---|---:|---:|
| `GBufferA` | 4 | 3,686,400 |
| `GBufferB` | 8 | 7,372,800 |
| `GBufferC` | 4 | 3,686,400 |
| `GBufferDepth` | 4 | 3,686,400 |
| **Total** | **20** | **18,432,000** |

## 2. Channel Layout

### 2.1 `GBufferA` — base color

| Channel | Value | Range | Encoding |
|---|---|---|---|
| R, G, B | glTF metallic-roughness `baseColor` | `[0, 1]` | Linear reflectance written to an sRGB render target. Hardware stores sRGB; an sRGB SRV returns linear to lighting. |
| A | Unused | Write `1` | Not a material flag. sRGB would distort packed bits. |

Source: `baseColorFactor * baseColorTexture` (texture sampled in linear). Missing texture
uses the factor only, default white `(1, 1, 1)`.

### 2.2 `GBufferB` — world normal and roughness

| Channel | Value | Range | Encoding |
|---|---|---|---|
| R, G, B | World-space shading normal | Unit vector, components in `[-1, 1]` | Raw `float16`. Not octahedral. Not remapped to `[0, 1]`. |
| A | Perceptual roughness | `[0, 1]` | glTF `roughnessFactor * metallicRoughnessTexture.g`. Do not store GGX `alpha`. |

The GBuffer pass writes `normalize(worldShadingNormal)`. Lighting may `normalize` again as
defense. Geometry normal is not stored separately; shading normal is the lighting input.

Normal maps are applied in the GBuffer pass (S1.4), not in lighting. Two-sided back faces
flip the normal as specified in [`renderer-conventions.md`](renderer-conventions.md).

### 2.3 `GBufferC` — metallic, AO, flags

| Channel | Value | Range | Encoding |
|---|---|---|---|
| R | Metallic | `[0, 1]` | glTF `metallicFactor * metallicRoughnessTexture.b` |
| G | Ambient occlusion | `[0, 1]` | glTF `occlusionTexture.r` scaled by `occlusionStrength`. Default `1` when no occlusion texture exists. |
| B | Material flags | `uint8` packed as UNORM | See below. |
| A | Unused | Write `1` | Reserved. |

Flag bits packed into channel B:

| Bit | Name | Meaning |
|---:|---|---|
| 0 | `ShadingValid` | `1` if this pixel has opaque GBuffer geometry. Lighting and reconstruction require it. |
| 1 | `TwoSided` | glTF `doubleSided`. Informational in Stage 1. |
| 2 | `AlphaTested` | Must be `0` in Stage 1 (opaque only). Reserved for S8.4. |
| 3–7 | reserved | Write `0`. |

Pack / unpack (CPU and HLSL must be identical):

```hlsl
float PackGBufferFlags(uint flags)
{
    return flags / 255.0;
}

uint UnpackGBufferFlags(float packed)
{
    return uint(packed * 255.0 + 0.5);
}
```

Eight-bit UNORM represents `0..255` exactly, so bits 0–7 survive a round trip.

Stage 1 writes `ShadingValid` on every rasterized opaque fragment. Background pixels keep
flags `0`.

### 2.4 `GBufferDepth` — reversed-Z depth

| View | DXGI format used by NVRHI | Consumer |
|---|---|---|
| Resource | `R32_TYPELESS` | Allocation |
| DSV | `D32_FLOAT` | GBuffer raster depth test / write |
| SRV | `R32_FLOAT` | Lighting reconstruction, debug linearized depth |

Stored value is device depth in `[0, 1]` with **clear = 0** (infinity). Near plane is `1`.

`nvrhi::TextureDesc` for S1.3:

- `format = nvrhi::Format::D32`
- `isTypeless = true`
- `isRenderTarget` is not required; depth-stencil + shader resource
- `useClearValue = true`, `clearValue = nvrhi::Color(0.f)`
- `initialState = nvrhi::ResourceStates::DepthWrite`

Color targets:

- `isRenderTarget = true`
- `useClearValue = true`
- `initialState = nvrhi::ResourceStates::RenderTarget`
- `clearValue` matching section 3

## 3. Clear Values and Background Semantics

Clear every target at the start of the GBuffer pass, before drawing.

| Resource | `nvrhi::Color` / depth | Background meaning |
|---|---|---|
| `GBufferA` | `(0, 0, 0, 1)` | No surface reflectance. Do not light this as a black dielectric. |
| `GBufferB` | `(0, 0, 0, 0)` | Not a unit normal. Roughness is undefined. |
| `GBufferC` | `(0, 0, 0, 1)` | Metallic `0`, AO `0`, flags `0` (`ShadingValid` off). AO is undefined for background. |
| `GBufferDepth` | depth `0`, no stencil | Empty / infinite far. |

A pixel is **background** if and only if `GBufferDepth == 0` (compare in the shader with
`deviceDepth <= 0.0`, which is exact for a cleared `D32` target). `ShadingValid == 0` must
agree; if a later bug writes a normal without depth, depth still wins.

Background lighting policy is defined in S2.1. Until then:

- Do not reconstruct world position.
- Do not evaluate a BRDF.
- Debug views show the clear encodings (black albedo, zero normal, far depth).

Foreground pixels must write all four resources, including `ShadingValid = 1` and a unit
normal.

## 4. Deferred Lighting Inputs

Each S2 lighting input has **exactly one** source. No second copy exists in another channel.

| Lighting input | Source | Notes |
|---|---|---|
| Linear base color | `GBufferA.rgb` via sRGB SRV | Unitless reflectance |
| World shading normal | `GBufferB.rgb` | Normalize after load |
| Perceptual roughness | `GBufferB.a` | Square in the BRDF, not here |
| Metallic | `GBufferC.r` | |
| Ambient occlusion | `GBufferC.g` | S2.1 decides how AO weights ambient versus direct light. This channel is the only AO texture. |
| Material flags | `GBufferC.b` unpacked | Background / two-sided / reserved |
| Device depth | `GBufferDepth` SRV | Reversed-Z |
| World position | Reconstructed from device depth + `matClipToWorld` | Formula in [`renderer-conventions.md`](renderer-conventions.md) |
| View / camera | Frame/view constant buffer from S1.2 | Not a GBuffer channel |
| Dielectric F0 / diffuse albedo | Derived in the lighting shader from base color and metallic | `F0 = 0.04` |
| Lights | Light buffer from S2 | Not a GBuffer channel |
| Shadows / DXR visibility | None in S2 | Optional declared lighting input in S7.4 |

`GBufferA.a` and `GBufferC.a` are not lighting inputs.

## 5. CPU and HLSL Declarations

These types are the implementation contract for S1.2 / S1.3 / S1.4. They are documentation
until those steps add the real headers. Do not invent additional channels when coding them.

```cpp
namespace renderlab
{
    inline constexpr uint32_t kGBufferFlagShadingValid = 1u << 0;
    inline constexpr uint32_t kGBufferFlagTwoSided     = 1u << 1;
    inline constexpr uint32_t kGBufferFlagAlphaTested  = 1u << 2;

    enum class GBufferTarget : uint32_t
    {
        A = 0,       // GBufferA, SV_Target0
        B = 1,       // GBufferB, SV_Target1
        C = 2,       // GBufferC, SV_Target2
        Depth = 3,   // GBufferDepth
        Count = 4
    };

    struct GBufferFormatDesc
    {
        const char* debugName;
        nvrhi::Format format;
        bool typeless;
        nvrhi::Color clearColor; // depth uses .r
    };

    // S1.3 must create textures using these exact names and formats.
    inline constexpr GBufferFormatDesc kGBufferFormats[] = {
        { "GBufferA",     nvrhi::Format::SRGBA8_UNORM, false, nvrhi::Color(0.f, 0.f, 0.f, 1.f) },
        { "GBufferB",     nvrhi::Format::RGBA16_FLOAT, false, nvrhi::Color(0.f) },
        { "GBufferC",     nvrhi::Format::RGBA8_UNORM,  false, nvrhi::Color(0.f, 0.f, 0.f, 1.f) },
        { "GBufferDepth", nvrhi::Format::D32,          true,  nvrhi::Color(0.f) },
    };

    inline float PackGBufferFlags(uint32_t flags)
    {
        return static_cast<float>(flags) / 255.0f;
    }

    inline uint32_t UnpackGBufferFlags(float packed)
    {
        return static_cast<uint32_t>(packed * 255.0f + 0.5f);
    }
}
```

HLSL GBuffer write (S1.4) and read (S2 / S1.5):

```hlsl
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
```

Cbuffer alignment for view matrices is 16 bytes per `float4` / 64 bytes per `float4x4`,
row-major, as in [`renderer-conventions.md`](renderer-conventions.md). S1.2 asserts CPU and
HLSL sizes.

## 6. Pass Parameters (S1.4)

`GBufferPass` inputs and outputs, named for later RDG migration:

**Inputs**

- Scene mesh draws (renderer-facing records from S1.2, not Donut internals in the shader)
- Frame/view constants
- Material parameters and optional fallback textures
- Instance world transforms

**Outputs**

- `GBufferA`, `GBufferB`, `GBufferC` (color writes)
- `GBufferDepth` (depth write)

The GPU marker name is `GBuffer`, nested under the existing `Render` marker. Do not rename
`Frame`, `SceneUpdate`, `Render`, `UI`, or `Present`.

## 7. Format Support

Required NVRHI `FormatSupport` bits, matching how NVRHI queries D3D12 (`rtvFormat` for RTV,
`srvFormat` for shader bits; depth uses `D32` → DSV `D32_FLOAT` and SRV `R32_FLOAT`):

| NVRHI format | Texture | RenderTarget | ShaderLoad | ShaderSample | DepthStencil |
|---|---|---|---|---|---|
| `SRGBA8_UNORM` | yes | yes | yes | yes | no |
| `RGBA16_FLOAT` | yes | yes | yes | yes | no |
| `RGBA8_UNORM` | yes | yes | yes | yes | no |
| `D32` (DSV) | yes | no | no | no | yes |
| `R32_FLOAT` (depth SRV) | yes | n/a | yes | yes | no |

Validated on 2026-08-19 against the S0.1 host GPU, NVIDIA GeForce RTX 4070 SUPER, D3D feature
level 12_2, driver `32.0.15.7688`. The probe also created a 64×64 `R32_TYPELESS` texture with
a `D32_FLOAT` DSV and an `R32_FLOAT` SRV.

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
New-Item -ItemType Directory -Force .\out\tmp-s11 | Out-Null
$out = ".\out\tmp-s11\check-gbuffer-formats.exe"
cmd /c "`"$vcvars`" && cl.exe /nologo /EHsc /std:c++20 /O2 /Fo.\out\tmp-s11\ /Fe:$out scripts\check-gbuffer-formats.cpp /link /nologo && `"$out`""
```

All five queries returned the required view support. Feature Level 11_0 already requires
these typed 2D views; the probe exists so a different adapter cannot silently change the
contract.

## 8. Explicitly Deferred

- Texture creation, resize, and debug UI byte counts: S1.3
- Mesh drawing and material evaluation: S1.4
- Channel debug views and screenshots: S1.5 / S1.6
- Position reconstruction in a live pass: S2.2
- HDR scene color, lights, and background fill color: S2
- Velocity, emissive GBuffer, MSAA, packed octahedral normals
