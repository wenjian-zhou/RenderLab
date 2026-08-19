# Renderer Data Contracts

Status: **frozen for Stage 1 after S1.2**

Step: S1.2

This file is the CPU/HLSL contract for frame, view, instance, and material data.
GBuffer packing stays in [`g-buffer.md`](g-buffer.md). Coordinate rules stay in
[`renderer-conventions.md`](renderer-conventions.md).

Donut loads glTF into `SceneGraph`, `MeshInfo`, `MeshGeometry`, and `Material`.
RenderLab maps those objects into renderer-owned records. GBuffer shaders must
consume these contracts, not Donut `GBufferFillPass`, `DeferredLightingPass`,
`PlanarViewConstants`, or `MaterialConstants`.

## 1. Headers

| File | Role |
|---|---|
| [`src/shaders/renderer_cb.h`](../src/shaders/renderer_cb.h) | Shared C++/HLSL cbuffers: `FrameConstants`, `ViewConstants`, `InstanceConstants`, `MaterialParams` |
| [`src/shaders/gbuffer_encoding.hlsli`](../src/shaders/gbuffer_encoding.hlsli) | GBuffer pack/encode/decode for S1.4 |
| [`src/renderer/GBufferContract.h`](../src/renderer/GBufferContract.h) | C++ GBuffer flags, formats, pack helpers |
| [`src/renderer/RendererData.h`](../src/renderer/RendererData.h) | Fill/convert/map API and `DrawRecord` |

HLSL files that include `renderer_cb.h` inherit `#pragma pack_matrix(row_major)`.
CPU copies `donut::math::float4x4` into the cbuffer with no transpose. Algebra is
row-vector: `p_out = mul(p_in, M)`.

## 2. Alignment

Payload sizes are 16-byte multiples. NVRHI/D3D12 constant-buffer views are 256-byte
aligned at bind time; that is an allocation rule, not a struct size.

| Struct | Size | Notable offsets |
|---|---:|---|
| `FrameConstants` | 16 | `frameIndex` @ 0 |
| `ViewConstants` | 448 | matrices @ 0/64/128/192/256/320; `viewportOrigin` @ 384; `zNear` @ 408; `flags` @ 412; `cameraPosition` @ 416; `verticalFovRadians` @ 432 |
| `InstanceConstants` | 128 | `matLocalToWorld` @ 0; `matWorldToLocal` @ 64 |
| `MaterialParams` | 32 | `baseColorFactor` @ 0; `roughness` @ 12; `metallic` @ 16; `occlusionStrength` @ 20; `normalScale` @ 24; `flags` @ 28 |

`RenderLabDataContractTests` asserts these sizes and offsets.

## 3. View filling

`MakeViewConstants` takes the live `FirstPersonCamera` world-to-view affine, the
S0.4 FOV in **degrees**, `zNear`, and the back-buffer viewport.

- `verticalFovRadians = radians(verticalFovDegrees)` before `perspProjD3DStyleReverse`
- `matWorldToClip = matWorldToView * matViewToClip`
- Inverses are stored for reconstruction
- `flags & RendererViewFlag_Mirrored` when `det(worldToView.linear) < 0`, which is
  always true for `FirstPersonCamera`. S1.4 raster state uses that as
  `frontCounterClockwise`

## 4. Donut → draw records

`BuildSceneDrawList` walks `SceneGraph::GetMeshInstances()`. Each triangle
`MeshGeometry` becomes one `DrawRecord`:

| Donut source | Draw record |
|---|---|
| `MeshInstance` node `GetLocalToWorldTransformFloat()` | `InstanceConstants` via `affineToHomogeneous` / `inverse` |
| `MeshInfo` name, `globalMeshIndex`, buffer handles | `meshName`, `meshIndex`, `GeometryDrawDesc` streams |
| `MeshGeometry` index/vertex offsets and counts | resolved `indexOffset` / `vertexOffset` / counts |
| `Material` metal-rough factors and texture slots | `MaterialParams` + `TextureBinding` |
| Vertex ranges | `VertexStream` for POSITION (`RGB32_FLOAT`), TEXCOORD_0 (`RG32_FLOAT`), NORMAL/TANGENT (`RGBA8_SNORM`) |
| Index buffer | `R32_UINT` (Donut upload format) |

Normal transform in S1.4: `normalize(mul(localNormal, transpose((float3x3)matWorldToLocal)))`.

## 5. Material conversion

Metal-rough only, matching [`g-buffer.md`](g-buffer.md):

```text
baseColor  = baseColorFactor * (HasBaseColorTexture ? baseColorTex.rgb : 1)
roughness  = roughnessFactor * (HasMetalRoughTexture ? orm.g : 1)
metallic   = metallicFactor  * (HasMetalRoughTexture ? orm.b : 1)
ao         = lerp(1, HasOcclusionTexture ? occlusion.r : 1, occlusionStrength)
```

Roughness stays perceptual. Do not square it here.

## 6. Fallback and unsupported data

Opaque fallback **values/textures apply only to legally optional glTF fields**.
Missing optional textures do not invent a second BRDF.

| Optional field | CPU fallback | Kind |
|---|---|---|
| `baseColorTexture` | `(1,1,1,1)` | `WhiteOpaque` |
| `metallicRoughnessTexture` | `(1,1,1,1)` so factors pass through | `WhiteOpaque` |
| `normalTexture` | geometry normal; 1x1 `(0.5,0.5,1,1)` if an SRV must be bound | `FlatNormal` |
| `occlusionTexture` | AO = 1; `occlusionStrength` forced to 0 | `OcclusionWhite` |

S1.2 does not create GPU 1x1 textures. S1.4 binds the scene texture or uploads the
documented pixel as a 1x1 SRV (`GBufferFallbackWhiteSrgb`, `GBufferFallbackWhite`,
`GBufferFallbackFlatNormal`, `GBufferFallbackOcclusion`).

The following are **unsupported required data** for this renderer. They are logged
and omitted from the opaque draw list; they are not silently converted:

- `KHR_materials_pbrSpecularGlossiness`
- Non-opaque domains (mask, blend, transmission)
- Hair or SSS Donut extensions
- Metalness packed in the red channel
- Skinned instances
- Non-triangle meshes/primitives
- Missing POSITION, index buffer, or NORMAL once Donut has created a `BufferGroup`

Emissive is ignored: it is not a Stage 1 GBuffer channel.

## 7. What this step does not do

No GBuffer textures, lighting, RDG, or DXR. Draw records are CPU data. S1.4
consumes them in `GBufferPass` (complete).
