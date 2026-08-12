# DXRLab Graphics Conventions

This document defines the graphics conventions shared by the CPU renderer, HLSL shaders, rasterization passes, ray-tracing passes, asset importers, and validation tools in DXRLab.

The keywords **MUST**, **MUST NOT**, **SHOULD**, and **SHOULD NOT** describe requirements rather than suggestions. A convention may change only through an explicit architecture decision record (ADR) and the corresponding validation updates.

## 1. Coordinate System and Units

DXRLab uses a left-handed world coordinate system:

- `+X` points right.
- `+Y` points up.
- `+Z` points forward.
- The default camera is located at the origin and looks along `+Z`.
- One world unit equals one meter.

Positions, translations, camera clipping distances, ray distances, and visibility ranges are expressed in meters. Velocities are expressed in meters per second. Direction vectors and normals are dimensionless and normalized where required.

The words *screen right* and *screen up* refer to the default camera orientation only. After the camera rotates, its local right and up vectors no longer coincide with world `+X` and `+Y`.

## 2. Vectors, Matrices, and Transform Composition

DXRLab follows the native DirectXMath convention:

- Vectors are row vectors.
- Matrices use row-major memory layout.
- HLSL transforms use `mul(vector, matrix)`.
- Transform composition proceeds from left to right.
- CPU matrices are uploaded without transposition.

The canonical transform chain is:

```cpp
DirectX::XMMATRIX worldViewProjection = world * view * projection;
```

```hlsl
row_major float4x4 WorldViewProjection;
float4 clipPosition = mul(float4(localPosition, 1.0), WorldViewProjection);
```

Every matrix declaration shared with CPU code MUST explicitly use `row_major`. Shader code MUST NOT mix `mul(vector, matrix)` and `mul(matrix, vector)` conventions.

DirectXMath left-handed helpers, including `XMMatrixLookAtLH`, `XMMatrixLookToLH`, and the appropriate left-handed projection helpers, MUST be used. Coordinate-system handedness is not inferred from the matrix library itself.

## 3. Clip Space and Reversed-Z Depth

DXRLab uses the Direct3D clip-space convention:

- NDC X and Y range from `-1` to `1`.
- NDC Z ranges from `0` to `1`.
- The main camera uses an infinite-far reversed-Z projection.
- The default near plane is `0.05 m`.

Under reversed-Z:

```text
Near plane   -> depth 1
Infinite far -> depth 0
```

The canonical depth state is:

```cpp
DepthClearValue = 0.0f;
DepthFunc       = D3D12_COMPARISON_FUNC_GREATER_EQUAL;
DepthWriteMask  = D3D12_DEPTH_WRITE_MASK_ALL;
```

The main depth resource SHOULD use:

```text
Resource format: DXGI_FORMAT_R32_TYPELESS
DSV format:      DXGI_FORMAT_D32_FLOAT
SRV format:      DXGI_FORMAT_R32_FLOAT
```

For a left-handed row-vector projection with an infinite far plane, the relevant reversed-Z relationship is:

```text
depth = nearPlane / viewSpaceZ
viewSpaceZ = nearPlane / depth
```

`depth == 0` represents the background or infinite distance and MUST be handled before linearization.

The projection far plane is infinite, but visibility is not. CPU culling, directional shadow rays, and GI rays MUST use an explicit finite visibility or tracing distance from scene configuration.

Algorithms that consume depth MUST follow the reversed ordering: a larger depth value is closer. This includes depth pyramids, occlusion tests, SSAO, SSR, temporal validation, and debug visualization. Shadow maps may define a separate depth convention when they are introduced.

## 4. Front Faces, Culling, and Mirrored Transforms

DXRLab defines clockwise triangles as front-facing.

The default rasterizer state is:

```cpp
FrontCounterClockwise = FALSE;
CullMode              = D3D12_CULL_MODE_BACK;
```

The canonical ray-tracing instance does not set `D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE`, so DXR and rasterization agree on clockwise front faces.

In v0.1, source node or instance transforms with a negative determinant are unsupported and MUST fail import with the offending node name or path. This check excludes the single global basis reflection used by the glTF-to-DXRLab coordinate conversion.

Later versions may support static mirrored transforms by baking the transform into duplicated geometry, reversing triangle winding, and repairing normal and tangent data. Animated scale that crosses zero remains unsupported because it produces singular transforms and discontinuous orientation.

## 5. glTF Import Convention

glTF 2.0 has a fixed source convention rather than per-file handedness or winding metadata. Relevant source rules include:

- Right-handed coordinates.
- `+Y` is up.
- Linear distances are measured in meters.
- Front faces use counter-clockwise winding.
- Camera local forward is `-Z`.

The importer converts glTF data once into the canonical DXRLab representation. The conversion MUST be centralized and MUST NOT be repeated in shaders.

The basis conversion includes:

- Reflecting the Z component of positions and direction-like attributes.
- Converting node translations, rotations, and matrices consistently.
- Reversing each triangle from `(i0, i1, i2)` to `(i0, i2, i1)`.
- Reflecting normal and tangent directions.
- Negating `tangent.w` so the reconstructed tangent frame preserves its orientation.

The importer MUST validate the complete transform and tangent frame with known-axis test assets. Changing only one quaternion component or only vertex positions is not a valid coordinate conversion.

## 6. UVs, Images, and Tangent-Space Normal Maps

DXRLab uses a top-left texture convention:

- UV `(0, 0)` is the top-left corner.
- `+U` points right.
- `+V` points down.
- glTF images and UV coordinates are imported without a vertical flip.

Screen pixel centers convert to normalized UV coordinates as follows:

```hlsl
float2 uv = (float2(pixelCoordinate) + 0.5) / float2(renderResolution);
```

A texture loader MUST NOT both reverse image rows and apply `uv.y = 1 - uv.y`. Asset-specific conversion, when unavoidable, must occur exactly once and be recorded by the importer.

Tangent-space normal maps use the `+Y` convention:

```hlsl
float3 tangentNormal = normalTexture.Sample(normalSampler, uv).xyz * 2.0 - 1.0;
```

The channels encode:

```text
Red   -> tangent-space X (Tangent)
Green -> tangent-space Y (Bitangent)
Blue  -> tangent-space Z (Surface Normal)
```

A flat normal map decodes from `(0.5, 0.5, 1.0)` to `(0, 0, 1)`. The green channel is not inverted.

The tangent frame is reconstructed using the converted glTF tangent sign:

```hlsl
float3 B = cross(N, T) * tangentSign;
```

Normal maps MUST be sampled as linear data, never through an sRGB view.

## 7. Color Space and Texture Semantics

DXRLab uses linear sRGB as its working space:

- RGB primaries match sRGB/Rec.709.
- Lighting, blending, interpolation, filtering, RTGI, and temporal accumulation occur in linear space.
- HDR scene color uses `R16G16B16A16_FLOAT` unless a pass documents another requirement.
- v0.1 outputs SDR sRGB.

Texture color space is selected by material semantics, not by file extension.

| Texture or value | Interpretation |
|---|---|
| Base-color texture | sRGB |
| Emissive texture | sRGB |
| Normal texture | Linear data |
| Metallic-roughness texture | Linear data |
| Occlusion texture | Linear data |
| Depth and motion vectors | Linear data |
| HDR environment texture | Linear data |
| Material factors and vertex colors | Linear values |

An sRGB SRV decodes RGB to linear values during sampling. Alpha is not transformed and remains a linear coverage or opacity value.

Color-texture mipmaps MUST be filtered in linear space:

```text
sRGB decode -> linear filter -> sRGB encode
```

Normal-map mip generation MUST preserve the vector interpretation and normalize filtered normals as required. Data textures MUST NOT receive sRGB conversion.

The display path is:

```text
Linear HDR scene color
-> exposure
-> tone mapping
-> linear SDR color
-> sRGB encode
-> swap chain
```

## 8. Material and BRDF Convention

DXRLab follows the glTF metallic-roughness material model.

The baseline BRDF is:

- GGX normal distribution.
- Smith GGX geometric visibility.
- Schlick Fresnel.
- Lambert diffuse.
- Perceptual roughness stored in material and GBuffer data.
- GGX alpha computed as `perceptualRoughness * perceptualRoughness`.
- Perceptual roughness clamped to a minimum of `0.04` for the baseline renderer.

Burley diffuse and alternative BRDF terms may be added only as explicit experiment modes. They MUST NOT silently replace the baseline used by benchmark scenes.

## 9. Photometric Light Units

Geometry is measured in meters, allowing punctual-light attenuation to use physical distance directly.

Directional lights store illuminance in lux:

```hlsl
float illuminance = light.illuminanceLux;
```

Point lights store luminous intensity in candela:

```hlsl
float illuminance = light.intensityCandela / max(distanceSquaredMeters, epsilon);
```

Spot lights also store center intensity in candela and apply an angular attenuation term:

```hlsl
float illuminance =
    light.intensityCandela
    * angularAttenuation
    / max(distanceSquaredMeters, epsilon);
```

The configured light range is a smooth cutoff and culling aid. It MUST NOT replace inverse-square attenuation.

Light color is stored as linear-sRGB chromaticity multiplied by the scalar photometric intensity. This is a practical RGB approximation to spectral photometry and must be described as such in technical reports.

## 10. glTF Emissive Mapping

Standard glTF emissive values are relative and do not specify a physical luminance unit. DXRLab establishes a reproducible project policy:

```text
1 glTF emissive unit = 100 nits by default
```

The conversion is:

```hlsl
float3 gltfEmission =
    sampledEmissiveTextureLinear
    * material.emissiveFactor
    * material.emissiveStrength;

float3 emissiveLuminance =
    gltfEmission * scene.gltfEmissiveReferenceNits;
```

Rules:

- `gltfEmissiveReferenceNits` defaults to `100.0`.
- Scene configuration may override the reference value.
- Benchmarks MUST record the effective reference value.
- The emissive texture is sampled through an sRGB view and returned as linear RGB.
- `emissiveFactor` and `emissiveStrength` are linear multipliers.
- Missing emissive texture data is treated as white before applying the factor.
- Missing `emissiveFactor` is black, producing no emission.
- Emissive alpha is ignored.
- Surface emission is added as emitted radiance and is not multiplied by base color, the BRDF, or `N dot L`.

## 11. Exposure and Tone Mapping

v0.1 uses deterministic manual exposure:

- Exposure is expressed as EV100.
- Exposure is applied before tone mapping.
- Automatic exposure is disabled.
- Pre-exposure is disabled.
- The baseline tone mapper is Khronos PBR Neutral.
- Benchmarks MUST record EV100 and MUST NOT adapt exposure during a run.

The baseline exposure scale is:

```hlsl
float exposureScale = 1.0 / (1.2 * exp2(EV100));
float3 exposedColor = sceneColor * exposureScale;
```

Alternative tone mappers are experiment modes and must not alter baseline comparison captures.

## 12. GBuffer and Screen-Space Data

The v0.1 GBuffer prioritizes correctness and observability over minimum bandwidth. Exact packing is intentionally deferred until captures identify a bandwidth or memory constraint.

The semantic conventions are fixed:

- Normals are stored in world space.
- Roughness is stored as perceptual roughness, not GGX alpha.
- The hardware reversed-Z depth buffer is sampled when needed; linear depth is reconstructed rather than stored redundantly.
- `depth == 0` identifies background pixels.
- Motion vectors use normalized UV units.
- Motion vectors use `R16G16_FLOAT` in the baseline implementation.

World-space normals allow rasterization, DXR, RTGI, and debug views to share a single representation. A later octahedral-normal experiment must preserve the same decoded world-space semantic.

## 13. Motion Vectors and Temporal History

Motion vectors map the current frame to the previous frame:

```text
motionVector = previousUV - currentUV
historyUV    = currentUV + motionVector
```

Rules:

- Motion is stored in normalized UV units.
- Geometry motion is calculated from unjittered current and previous transforms.
- Projection jitter is recorded separately in pixel units.
- Reprojection adds the corresponding `previousJitter - currentJitter` UV delta.
- A static object viewed by a static camera has a zero geometry motion vector.
- Dynamic objects retain their previous world transform.

Temporal history MUST be invalidated on at least:

- Camera cuts or teleports.
- Render-resolution changes.
- Projection or field-of-view changes.
- Scene replacement.
- Explicit temporal reset requests.

History validation uses screen bounds, reversed-Z depth, and world-space normals. Exact thresholds are renderer settings and must be recorded by benchmark scenes.

## 14. DXR Ray Convention

Application-level rays use world-space metric semantics:

- Origin is in world space and measured in meters.
- Direction is normalized before tracing.
- `TMin`, `TMax`, and `RayTCurrent()` are measured in meters.

World-space hit position is reconstructed as:

```hlsl
float3 hitPositionWS =
    WorldRayOrigin()
    + WorldRayDirection() * RayTCurrent();
```

DXR may transform a ray into object space using a non-uniform instance transform. `ObjectRayDirection()` is therefore not guaranteed to remain normalized and MUST NOT be renormalized, because doing so changes the ray parameterization.

Camera rays use the camera near plane as `TMin` and an explicit visibility distance as `TMax`. Surface-spawned shadow and GI rays use a centralized origin-offset function and then set `TMin` to zero. Point and spot shadow rays terminate at the light distance; directional and GI rays use configured finite trace ranges.

Surface-spawned rays MUST call a shared robust function equivalent to:

```hlsl
float3 OffsetRayOrigin(
    float3 positionWS,
    float3 geometricNormalWS,
    float3 outgoingDirectionWS);
```

The implementation uses the geometric normal and a scale-aware or ULP-aware offset. Scattered fixed constants such as `position += normal * 0.001` are forbidden.

## 15. Geometric and Shading Normals

DXRLab maintains two distinct world-space normals at a surface hit:

- `Ng`: geometric normal derived from triangle geometry.
- `Ns`: shading normal derived from interpolated vertex normals and the normal map.

`Ng` is used for:

- Front/back classification.
- Ray-origin offset.
- Selecting the side from which a secondary ray is emitted.
- Geometric visibility and consistency checks.

`Ns` is used for:

- BRDF evaluation.
- Direct lighting.
- RTGI shading.
- Reflection-direction calculation.

Normal maps MUST NOT modify `Ng`. If vertex normals are absent, `Ns` defaults to `Ng`.

After normal mapping, `Ns` MUST remain in the hemisphere of `Ng`:

```hlsl
dot(Ns, Ng) >= 0
```

v0.1 performs a robust hemisphere correction. A later GI implementation may add a shading-normal energy correction, but it must preserve the `Ng`/`Ns` distinction.

Triangle attributes use barycentric interpolation:

```hlsl
float3 weights = float3(
    1.0 - attributes.barycentrics.x - attributes.barycentrics.y,
    attributes.barycentrics.x,
    attributes.barycentrics.y);
```

## 16. DXR Culling, Alpha Modes, and Double-Sided Materials

Rasterization and DXR use the same material-visibility rules.

### Opaque

- Back faces are culled unless the material is double-sided.
- BLAS geometry is marked opaque.
- No any-hit shader is used.

### Alpha Mask

Rasterization and DXR use the same alpha expression:

```hlsl
float alpha = material.baseColorFactor.a * sampledBaseColor.a;
bool accepted = alpha >= material.alphaCutoff;
```

The glTF default alpha cutoff is `0.5`. Alpha-mask BLAS geometry is not marked opaque. Rasterization discards rejected fragments; the DXR any-hit shader calls `IgnoreHit()` for rejected intersections.

Ray-tracing shaders do not rely on implicit pixel derivatives. The v0.1 any-hit path uses explicit `SampleLevel(..., 0)`. Ray-cone-based mip selection is deferred.

### Alpha Blend

Alpha-blended materials are unsupported in v0.1 DXR and MUST produce an explicit import or feature error. They MUST NOT be silently treated as opaque or alpha-masked.

### Double-Sided

Double-sided materials disable raster back-face culling. Their corresponding ray-tracing instance sets:

```cpp
D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE
```

For a back-face hit on a double-sided material, both `Ng` and `Ns` are flipped before shading so that they face the incident side consistently.

The DXR cull-disable flag applies at TLAS-instance granularity. A source mesh containing both single-sided and double-sided primitives MUST be partitioned into compatible ray-tracing instances or BLAS groups.

Shadow rays use the equivalent of:

```text
RAY_FLAG_CULL_BACK_FACING_TRIANGLES
RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
RAY_FLAG_SKIP_CLOSEST_HIT_SHADER
```

Alpha-mask geometry still executes its any-hit shader. Only an intersection that passes the alpha test terminates the shadow query.

## 17. Deferred Conventions

The following choices are deliberately deferred until their implementing feature or experiment begins:

- Exact GBuffer channel packing and octahedral-normal encoding.
- Bindless instance, geometry, material, and primitive ID layout.
- Ray-cone or ray-differential texture mip selection.
- HDR10 output, automatic exposure, and pre-exposure.
- Alpha-blended ray-tracing materials.
- Runtime or animated negative-scale support.
- Shadow-map depth convention.
- Cubemap face and environment-map orientation.

Deferred items MUST NOT be assigned an undocumented implicit convention in production code.

## 18. Required Validation Cases

The following tests or debug scenes establish that the conventions agree across CPU code, rasterization, and DXR:

1. The default camera sees geometry placed on `+Z`; world `+X` appears right and `+Y` appears up.
2. A DirectXMath matrix uploaded without transposition produces the same transform in CPU reference code and HLSL `mul(vector, matrix)`.
3. Near-plane reversed-Z depth is `1`, background depth is `0`, and `GREATER_EQUAL` accepts the nearest surface.
4. A clockwise triangle is front-facing in both rasterization and DXR.
5. A glTF axis-and-winding test asset preserves front faces, normals, tangents, and UV orientation after import.
6. A flat `(0.5, 0.5, 1.0)` normal map decodes to `(0, 0, 1)`.
7. Sampling sRGB `0.5` returns approximately `0.214` linear, while a linear data texture preserves `0.5`.
8. A static object and static camera produce a zero geometry motion vector.
9. A normalized world ray reports `RayTCurrent()` in meters, and object-space transformation does not change the hit position.
10. Raster alpha test and DXR any-hit accept and reject the same texels at mip level zero.

These cases are baseline correctness checks, not optional visual polish.
