# Third-Party Notices

S0.1 selected the Donut/NVRHI baseline and recorded exact revisions in
[`dependencies.lock.json`](dependencies.lock.json). The sources themselves are
not vendored yet; S0.2 will add Donut under `external/donut` at the locked
commit. License texts below are taken from the inspected upstream trees so the
baseline can be reviewed before the submodule exists.

This project does not use vcpkg at the S0.1 baseline.

## NVIDIA Donut

- Source: https://github.com/NVIDIA-RTX/Donut
- Commit: `bfdebdd7dd5455c503b2737a1967a4ef651c145b`
- License: MIT

```text
Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```

## NVIDIA NVRHI

- Source: https://github.com/NVIDIA-RTX/NVRHI
- Commit: `8e8c36e37558acec333204619b95d9d2fcdc4a79`
- License: MIT
- Relationship: exact Donut submodule; not selected independently

The NVRHI `LICENSE.txt` at that commit uses the same MIT text as Donut, with
copyright `Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.`

## NVIDIA ShaderMake

- Source: https://github.com/NVIDIA-RTX/ShaderMake
- Commit: `5daebdbef45088fc2369d441391ecab0eba25e54`
- License: MIT

Copyright at that commit: `Copyright (c) 2014-2023, NVIDIA CORPORATION. All rights reserved.`
The license body matches the MIT text quoted for Donut.

## Dear ImGui

- Source: https://github.com/ocornut/imgui
- Commit: `45acd5e0e82f4c954432533ae9985ff0e1aad6d5`
- Version label: 1.92.2b
- License: MIT
- Copyright: Copyright (c) 2014-2025 Omar Cornut

## GLFW

- Source: https://github.com/glfw/glfw
- Commit: `7b6aead9fb88b3623e3b3725ebb42670cbe4c579`
- Version label: 3.4
- License: Zlib

```text
Copyright (c) 2002-2006 Marcus Geelnard
Copyright (c) 2006-2019 Camilla Löwy

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would
   be appreciated but is not required.

2. Altered source versions must be plainly marked as such, and must not
   be misrepresented as being the original software.

3. This notice may not be removed or altered from any source
   distribution.
```

## cgltf

- Source: https://github.com/jkuhlmann/cgltf
- Commit: `fa3b80fa762790192c9532b63c441627416ff300`
- License: MIT
- Copyright: Copyright (c) 2018 Johannes Kuhlmann

## stb

- Source: https://github.com/nothings/stb
- Commit: `2e2bef463a5b53ddf8bb788e25da6b8506314c08`
- License: MIT OR Unlicense
- Copyright: Copyright (c) 2017 Sean Barrett

## JsonCpp

- Source: https://github.com/open-source-parsers/jsoncpp
- Version: 1.9.6
- Location after S0.2: `external/donut/thirdparty/jsoncpp-amalgam`
- License: Public Domain or MIT, per the amalgam `LICENSE` shipped by Donut

## DirectX-Headers

- Source: https://github.com/microsoft/DirectX-Headers
- Commit: `d873b344dc540898868697245f100c2a67fe68d9`
- Upstream tag resolved: `v1.717.0-preview`
- License: MIT
- Copyright: Copyright (c) Microsoft Corporation
- Acquisition: NVRHI CMake FetchContent, with the tag replaced by the locked commit

## DirectX Shader Compiler (DXC)

- Source: https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2602
- Version: v1.9.2602
- Windows asset: `dxc_2026_02_20.zip`
- SHA-256: `A1E89031421CF3C1FCA6627766AB3020CA4F962AC7E2CAA7FAB2B33A8436151E`
- License: University of Illinois Open Source License
- Acquisition: ShaderMake CMake FetchContent of the official release zip

DXC is a compiler binary fetched at configure time. It is not a Git submodule.

## Intentionally Not Acquired

The following packages exist in Donut/NVRHI option space but are not part of
the S0.1 baseline and must stay disabled or unfetched in S0.2:

- Vulkan-Headers
- Microsoft Direct3D 12 Agility SDK
- Streamline
- DLSS
- RTXMU
- NVAPI
- Slang
- Donut-Samples (reference only)

tinyexr is present as a Donut third-party header and remains optional through
`DONUT_WITH_TINYEXR`. It is not a selected RenderLab-owned dependency.
