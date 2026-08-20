# Capture Guide

This file is the Stage 0 / M0 capture checklist, plus the S1.3 GBuffer resource
names and S1.4 opaque MRT writes to look for in PIX. Do not commit capture files.

S1.5 can dump visualized GBuffer channels with `--dump-gbuffer-views`. That is a
channel dump, not image regression. `--output` remains unimplemented until S1.6.

## Marker Names

RenderLab emits the same five names in Debug and Release:

| Name | CPU / command-queue event | GPU command-list event |
|---|---|---|
| `Frame` | around the whole DeviceManager frame | parent range on the app command list |
| `SceneUpdate` | around camera / HUD update | around `Scene::Refresh` |
| `Render` | around the app render pass | around the back-buffer clear |
| `UI` | around the ImGui renderer | Donut also records `ImGUI` for the actual draws |
| `Present` | around DXGI present | standalone named range submitted just before present |

Do not rename these strings. Later passes add nested markers under `Render`.
S1.3 adds `GBuffer` under `Render` for the target clears. S1.4 writes meshes
inside that same marker and adds a timestamp query around the clears and draws.
S1.5 adds `GBufferDebug` under `Render`, after `GBuffer`. Do not rename
`Frame`, `SceneUpdate`, `Render`, `GBuffer`, `UI`, or `Present`.

A PIX 2603 GPU capture of this revision shows this hierarchy (Debug and Release
are identical):

```text
Frame
  SceneUpdate
  Render
    Frame
      SceneUpdate
      Render
        GBuffer           (ClearRenderTargetView / ClearDepthStencilView, then DrawIndexed)
        GBufferDebug      (fullscreen DecodeGBuffer draw to the back buffer)
  UI
    ImGUI             (Donut ImGui draws)
  Present
    Present           (standalone GPU range)
    Present           (DXGI Present)
```

Windows SDK `pix.h` events are displayed with a PIX deprecation prefix
(`<deprecated - use pix3.h instead>`). The names after that prefix are the
stable strings above. WinPixEventRuntime is not added as a dependency for M0.

## Local PIX Capture

Validated host: PIX 2603.25 at `C:\Program Files\Microsoft PIX\2603.25`.

Interactive:

1. Build Debug or Release.
2. Start PIX and attach to, or launch,
   `out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera`.
3. Take a GPU capture after the first presented frame.
4. Confirm the event list contains `Frame`, `SceneUpdate`, `Render`, `UI` or
   `ImGUI`, and `Present`.
5. Confirm NVRHI/D3D12 validation printed no error or corruption message.

Command-line (preferred for evidence):

```powershell
$pix = "C:\Program Files\Microsoft PIX\2603.25\pixtool.exe"
$exe = ".\out\build\windows-vs2022\bin\Debug\RenderLab.exe"
New-Item -ItemType Directory -Force captures | Out-Null

& $pix launch $exe --command-line="--lock-camera --frames 16" `
  take-capture --frames=1 `
  save-capture captures\s05-m0.wpix

& $pix open-capture captures\s05-m0.wpix `
  save-event-list captures\s05-m0-events.csv
```

Inspect `captures\s05-m0-events.csv` for the stable names. Repeat with the
Release executable and confirm the names are identical.

Do not commit `*.wpix` files or `captures/`. Those artifacts are generated and
machine-specific.

## M0 Checklist

A fresh clone satisfies M0 when all of the following are true:

- [ ] `powershell -NoProfile -File scripts\bootstrap.ps1`
- [ ] `cmake --fresh --preset windows-vs2022`
- [ ] `cmake --build --preset windows-debug --parallel`
- [ ] `cmake --build --preset windows-release --parallel`
- [ ] `.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --frames 30` loads
      `scenes/cesium-milk-truck/CesiumMilkTruck.glb`, prints camera preset
      `s04-default`, and exits 0
- [ ] The same command works for Release
- [ ] `.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless` hides the
      window, presents 8 frames, and exits 0
- [ ] A PIX GPU capture shows the stable marker names
- [ ] NVRHI/D3D12 validation produces no error or corruption messages
- [ ] No Donut or NVRHI source was modified

"Render" at M0 still meant clear + UI + present. S1.4 rasterizes opaque meshes into
the GBuffer under the nested `GBuffer` marker. S1.5 presents the selected GBuffer
debug view on the back buffer (plus UI) under `GBufferDebug`. S1.3 persistent
resources that PIX should list at back-buffer width × height are
`GBufferA`/`B`/`C`/`GBufferDepth` with formats `R8G8B8A8_UNORM_SRGB`,
`R16G16B16A16_FLOAT`, `R8G8B8A8_UNORM`, and `R32_TYPELESS` (`D32_FLOAT` DSV /
`R32_FLOAT` SRV).

## S1.3 GBuffer Resource Capture

After a GPU capture, inspect the resource list for the four debug names above.
A `--frames 30` windowed run also resizes 1280×720 → 1344×784 at frame 8, then
minimizes and restores around frame 12. Confirm the capture taken after resize
shows 1344×784, and that validation printed no use-after-free or state error.

```powershell
$pix = "C:\Program Files\Microsoft PIX\2603.25\pixtool.exe"
$exe = ".\out\build\windows-vs2022\bin\Debug\RenderLab.exe"
New-Item -ItemType Directory -Force captures | Out-Null

& $pix launch $exe --command-line="--lock-camera --frames 16" `
  take-capture --frames=1 `
  save-capture captures\s13-gbuffer.wpix

& $pix open-capture captures\s13-gbuffer.wpix `
  save-event-list captures\s13-gbuffer-events.csv
```

Do not commit the capture files.

## S1.4 Opaque GBuffer Writes

After a GPU capture of `--lock-camera`, confirm:

- Event list: `Render / GBuffer` contains the four clears and at least one `DrawIndexed`
- Pixel History / resource viewer: `GBufferA`, `GBufferB`, `GBufferC` are bound as RTVs
  together with `GBufferDepth` as the DSV
- Background pixels keep the S1.3 clears; opaque foreground writes `ShadingValid` and
  reversed-Z depth greater than 0
- Camera matches the S0.4 preset; instance transforms match S1.2 draw records

```powershell
$pix = "C:\Program Files\Microsoft PIX\2603.25\pixtool.exe"
$exe = ".\out\build\windows-vs2022\bin\Debug\RenderLab.exe"
New-Item -ItemType Directory -Force captures | Out-Null

& $pix launch $exe --command-line="--lock-camera --frames 16" `
  take-capture --frames=1 `
  save-capture captures\s14-gbuffer.wpix

& $pix open-capture captures\s14-gbuffer.wpix `
  save-event-list captures\s14-gbuffer-events.csv
```

Do not commit the capture files.

## S1.5 GBuffer Debug Visualization

After a GPU capture of `--lock-camera`, confirm:

- Event list: `Render / GBuffer` still has the four clears and `DrawIndexed`
- Event list: `Render / GBufferDebug` has a fullscreen `Draw` (3 vertices) after `GBuffer`
- The back buffer is the selected debug view, not the S0.5 clear color
- `GBufferDepth` is sampled as an `R32_FLOAT` SRV; no second depth texture is created

```powershell
$pix = "C:\Program Files\Microsoft PIX\2603.25\pixtool.exe"
$exe = ".\out\build\windows-vs2022\bin\Debug\RenderLab.exe"
New-Item -ItemType Directory -Force captures | Out-Null

& $pix launch $exe --command-line="--lock-camera --frames 16" `
  take-capture --frames=1 `
  save-capture captures\s15-gbuffer-debug.wpix

& $pix open-capture captures\s15-gbuffer-debug.wpix `
  save-event-list captures\s15-gbuffer-debug-events.csv
```

Dump every mandatory visualized channel (not S1.6 golden images):

```powershell
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --lock-camera --dump-gbuffer-views captures\s15-views
```

That writes:

- `gbuffer-base-color.png`
- `gbuffer-world-normal.png`
- `gbuffer-roughness.png`
- `gbuffer-metallic.png`
- `gbuffer-ao-flags.png`
- `gbuffer-linear-depth.png`

Do not commit the PNG files or the PIX capture.

## Smoke And CI

The fixed-frame smoke command is:

```powershell
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless
.\out\build\windows-vs2022\bin\Release\RenderLab.exe --headless --frames 8
```

Or:

```powershell
powershell -NoProfile -File scripts\smoke.ps1
```

`--headless` is a CI-safe startup mode, not a swap-chain-free device. Donut
`DeviceManager` still owns the window, D3D12 device, queues, fences, and swap
chain. The window is hidden and the camera is locked so the process can exit
without interactive input.

GitHub-hosted `windows-2022` runners do not expose the required NVIDIA D3D12
environment. The workflow therefore stays configure + build, and smoke remains
a documented local test. A self-hosted runner with an NVIDIA GPU can run
`scripts\smoke.ps1` after the build.
