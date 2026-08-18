# RenderLab Scenes

S0.4 pins one deterministic scene and camera pair for later raster, RDG, and DXR
image tests.

## Default scene: Cesium Milk Truck

- Path: `scenes/cesium-milk-truck/CesiumMilkTruck.glb`
- Source: Khronos [glTF-Sample-Assets](https://github.com/KhronosGroup/glTF-Sample-Assets)
  commit `97cb805c6f47bc9449e250bd795c34149f0a870e`
- Why: official redistributable glTF 2.0 asset with multiple meshes/nodes,
  basic metallic-roughness materials, and a committed size of 370 KB
- License: CC-BY 4.0 with Cesium trademark limitations; see
  `cesium-milk-truck/LICENSE.md`

The file is small enough to commit, so S0.4 does not add a download script.

## Fallback scene: Fallback Boxes

- Path: `scenes/fallback/boxes.gltf`
- Three authored meshes (ground, dielectric box, metal box) with distinct
  metallic-roughness factors
- License: CC0 1.0 Universal

Use `--scene fallback-boxes` if you need the committed fallback. The default
startup path does not silently switch scenes when the milk truck is missing or
corrupt; it fails with an actionable message instead.

## Integrity

`manifest.json` records URL, license, and SHA-256 for every committed scene
file. RenderLab verifies the selected scene hash before Donut loads it.

## Camera preset

The S0.4 camera is recorded in `manifest.json` as `s04-default`. Startup always
applies that LookAt. Pass `--lock-camera` to disable first-person motion so the
view stays on the preset.
