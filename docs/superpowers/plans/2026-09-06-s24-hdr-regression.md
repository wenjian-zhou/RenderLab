# S2.4 HDR Regression Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close Stage 2 by capturing, finite-checking, and comparing a locked `HDRSceneColor` dump without changing S1.6 GBuffer goldens.

**Architecture:** Header-only `.rlhdr` IO and finite scan live in `src/renderer/HdrDump.h` (no NVRHI, no BRDF). CPU compare and metadata parsing live in `tests/hdr_compare.*`, mirroring `tests/image_compare.*`. The app readbacks `HDRSceneColor` into tightly packed halves, aborts with no files on NaN/Inf, then writes `.rlhdr` + Reinhard `lighting-lit.png` + `hdr-capture-metadata.json` behind `--output-hdr`. `RenderLabGoldenCompare.exe --mode hdr` compares RGB with mixed abs/rel error. Tight numeric tolerances are calibrated in Task 9 from the first approved GPU capture.

**Tech Stack:** C++20, MSVC, NVRHI D3D12 staging textures, jsoncpp, existing LightingDebug Reinhard path, PowerShell `scripts/golden-hdr.ps1`.

**Contract:** [`docs/hdr-regression.md`](../../hdr-regression.md)

---

## File map

| File | Responsibility |
|---|---|
| `src/renderer/HdrDump.h` | Packed `.rlhdr` header, half↔float, finite RGB scan, load/save |
| `tests/hdr_compare.h` | Locked HDR identity constants, compare stats, metadata, directory compare |
| `tests/hdr_compare.cpp` | jsoncpp metadata parse, mixed abs/rel compare, reports |
| `tests/test_hdr_compare.cpp` | CPU tests (no GPU, no BRDF port) |
| `tests/golden_compare_main.cpp` | `--mode hdr` and HDR `--channel-swap` |
| `tests/CMakeLists.txt` | Compile new sources; GoldenCompare include `src/` |
| `tests/test_renderer_data.cpp` | Call `RunHdrCompareTests()` |
| `src/app/main.cpp` | `--output-hdr`, exclusive with `--output`, dump hook |
| `src/app/RenderingLabApp.h/.cpp` | `DumpHdrCapture`, `WriteHdrCaptureMetadata` |
| `scripts/golden-hdr.ps1` | Capture / Compare / Swap / Verify |
| `tests/golden-hdr/...` | Committed oracle after Task 9 |
| `docs/hdr-regression.md` | Fill tolerance table after Task 9 |

Do not modify `scripts/golden.ps1` or S1.6 `--output` behavior. Do not port `EvaluateDirectBRDF`.

---

### Task 1: `.rlhdr` format, half conversion, finite scan

**Files:**
- Create: `src/renderer/HdrDump.h`
- Create: `tests/test_hdr_compare.cpp`
- Modify: `src/CMakeLists.txt` (add `renderer/HdrDump.h` to `RENDERLAB_RENDERER_SOURCES`)
- Modify: `tests/CMakeLists.txt` (add `test_hdr_compare.cpp` to `RenderLabDataContractTests`)
- Modify: `tests/test_renderer_data.cpp` (declare and call `RunHdrCompareTests`)

- [ ] **Step 1: Write the failing tests**

Create `tests/test_hdr_compare.cpp`:

```cpp
#include "renderer/HdrDump.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <vector>

using namespace renderlab;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* message)
    {
        if (condition)
        {
            std::printf("  PASS  %s\n", message);
            std::fflush(stdout);
            return;
        }
        std::printf("  FAIL  %s\n", message);
        std::fflush(stdout);
        ++g_failures;
    }
}

int RunHdrCompareTests()
{
    std::printf("RenderLab S2.4 HDR dump / compare tests\n");
    std::fflush(stdout);

    Check(sizeof(RlHdrHeader) == 32, "RlHdrHeader is 32 bytes");
    Check(kRlHdrVersion == 1, "rlhdr version is 1");
    Check(kRlHdrFormatRgba16Float == 1, "format enum 1 is RGBA16_FLOAT");
    Check(std::memcmp(kRlHdrMagic, "RLHDR1", 6) == 0, "magic starts with RLHDR1");
    Check(kRlHdrMagic[6] == '\0' && kRlHdrMagic[7] == '\0', "magic trailing bytes are NUL");

    Check(HalfToFloat(FloatToHalf(0.f)) == 0.f, "half 0 round-trips");
    Check(std::fabs(HalfToFloat(FloatToHalf(1.f)) - 1.f) < 1e-3f, "half 1 round-trips");
    Check(std::fabs(HalfToFloat(FloatToHalf(4.f)) - 4.f) < 1e-2f, "half 4 round-trips");
    Check(!IsFiniteHalf(kHalfPositiveInf), "+Inf half is non-finite");
    Check(!IsFiniteHalf(kHalfNan), "NaN half is non-finite");
    Check(IsFiniteHalf(FloatToHalf(0.5f)), "0.5 half is finite");

    std::vector<uint16_t> pixels(4 * 4, 0);
    pixels[0] = FloatToHalf(0.25f);
    pixels[1] = FloatToHalf(0.5f);
    pixels[2] = FloatToHalf(1.f);
    pixels[3] = FloatToHalf(1.f);
    Check(CountNonFiniteRgb(pixels.data(), 2, 2) == 0, "finite 2x2 counts 0");
    pixels[4] = kHalfNan;
    Check(CountNonFiniteRgb(pixels.data(), 2, 2) == 1, "NaN in R counts 1 texel");
    pixels[4] = FloatToHalf(0.f);
    pixels[5] = kHalfPositiveInf;
    Check(CountNonFiniteRgb(pixels.data(), 2, 2) == 1, "Inf in G counts 1 texel");
    pixels[5] = FloatToHalf(0.f);
    pixels[7] = kHalfNan;
    Check(CountNonFiniteRgb(pixels.data(), 2, 2) == 0, "NaN in A is ignored");

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "renderlab-s24-roundtrip.rlhdr";
    std::string error;
    Check(WriteRlHdrFile(path, 2, 2, pixels.data(), error), "Write 2x2 rlhdr");
    RlHdrImage loaded;
    Check(LoadRlHdrFile(path, loaded, error), "Load 2x2 rlhdr");
    Check(loaded.width == 2 && loaded.height == 2, "Loaded size is 2x2");
    Check(loaded.rgba16.size() == 16, "Loaded 16 half values");
    Check(loaded.rgba16[0] == pixels[0] && loaded.rgba16[2] == pixels[2], "RGB halves round-trip");
    Check(std::filesystem::file_size(path) == 32 + 2 * 2 * 8, "File size is header plus tightly packed pixels");

    RlHdrImage bad;
    Check(!LoadRlHdrFile(path / "missing.rlhdr", bad, error), "Missing file fails");
    Check(!error.empty(), "Missing file sets error");

    return g_failures;
}
```

Add to `tests/test_renderer_data.cpp` with the other declarations:

```cpp
int RunHdrCompareTests();
```

and before printing failures:

```cpp
    g_failures += RunHdrCompareTests();
```

Add `test_hdr_compare.cpp` to `RenderLabDataContractTests` sources in `tests/CMakeLists.txt`.
Add `renderer/HdrDump.h` to `RENDERLAB_RENDERER_SOURCES` in `src/CMakeLists.txt`.

- [ ] **Step 2: Run tests and confirm they fail to compile / fail**

```powershell
cmake --build --preset windows-debug --parallel --target RenderLabDataContractTests
```

Expected: compile error `HdrDump.h` not found, or undefined `RlHdrHeader`.

- [ ] **Step 3: Implement `src/renderer/HdrDump.h`**

```cpp
#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace renderlab
{
    inline constexpr char kRlHdrMagic[8] = {'R', 'L', 'H', 'D', 'R', '1', '\0', '\0'};
    inline constexpr uint32_t kRlHdrVersion = 1;
    inline constexpr uint32_t kRlHdrFormatRgba16Float = 1;
    inline constexpr uint32_t kRlHdrHeaderBytes = 32;
    inline constexpr uint16_t kHalfPositiveInf = 0x7c00;
    inline constexpr uint16_t kHalfNan = 0x7e00;

#pragma pack(push, 1)
    struct RlHdrHeader
    {
        char magic[8];
        uint32_t version;
        uint32_t width;
        uint32_t height;
        uint32_t format;
        uint32_t reserved0;
        uint32_t reserved1;
    };
#pragma pack(pop)

    static_assert(sizeof(RlHdrHeader) == kRlHdrHeaderBytes);

    struct RlHdrImage
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint16_t> rgba16;
    };

    inline uint16_t FloatToHalf(float value)
    {
        union
        {
            float f;
            uint32_t u;
        } in;
        in.f = value;
        const uint32_t sign = (in.u >> 16) & 0x8000u;
        uint32_t mantissa = in.u & 0x7fffffu;
        int32_t exponent = static_cast<int32_t>((in.u >> 23) & 0xffu) - 127 + 15;
        if ((in.u & 0x7fffffffu) > 0x7f800000u)
        {
            return static_cast<uint16_t>(sign | 0x7e00u);
        }
        if (exponent <= 0)
        {
            if (exponent < -10)
            {
                return static_cast<uint16_t>(sign);
            }
            mantissa |= 0x800000u;
            const uint32_t shift = static_cast<uint32_t>(14 - exponent);
            const uint32_t half = mantissa >> shift;
            return static_cast<uint16_t>(sign | half);
        }
        if (exponent >= 31)
        {
            return static_cast<uint16_t>(sign | 0x7c00u);
        }
        return static_cast<uint16_t>(
            sign | (static_cast<uint32_t>(exponent) << 10) | (mantissa >> 13));
    }

    inline float HalfToFloat(uint16_t bits)
    {
        const uint32_t sign = (static_cast<uint32_t>(bits) & 0x8000u) << 16;
        uint32_t exponent = (bits >> 10) & 0x1fu;
        uint32_t mantissa = bits & 0x3ffu;
        uint32_t out = 0;
        if (exponent == 0)
        {
            if (mantissa == 0)
            {
                out = sign;
            }
            else
            {
                exponent = 1;
                while ((mantissa & 0x400u) == 0)
                {
                    mantissa <<= 1;
                    --exponent;
                }
                mantissa &= 0x3ffu;
                out = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
            }
        }
        else if (exponent == 31)
        {
            out = sign | 0x7f800000u | (mantissa << 13);
        }
        else
        {
            out = sign | ((exponent + 127 - 15) << 23) | (mantissa << 13);
        }
        union
        {
            uint32_t u;
            float f;
        } conv;
        conv.u = out;
        return conv.f;
    }

    inline bool IsFiniteHalf(uint16_t bits)
    {
        return ((bits & 0x7c00u) != 0x7c00u);
    }

    inline uint64_t CountNonFiniteRgb(const uint16_t* rgba16, uint32_t width, uint32_t height)
    {
        uint64_t count = 0;
        const uint64_t pixels = static_cast<uint64_t>(width) * height;
        for (uint64_t i = 0; i < pixels; ++i)
        {
            const uint16_t r = rgba16[i * 4u + 0u];
            const uint16_t g = rgba16[i * 4u + 1u];
            const uint16_t b = rgba16[i * 4u + 2u];
            if (!IsFiniteHalf(r) || !IsFiniteHalf(g) || !IsFiniteHalf(b))
            {
                ++count;
            }
        }
        return count;
    }

    inline bool WriteRlHdrFile(
        const std::filesystem::path& path,
        uint32_t width,
        uint32_t height,
        const uint16_t* rgba16,
        std::string& error)
    {
        error.clear();
        if (width == 0 || height == 0 || rgba16 == nullptr)
        {
            error = "rlhdr write requires a non-empty image";
            return false;
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            error = "failed to create " + path.generic_string();
            return false;
        }
        RlHdrHeader header{};
        std::memcpy(header.magic, kRlHdrMagic, 8);
        header.version = kRlHdrVersion;
        header.width = width;
        header.height = height;
        header.format = kRlHdrFormatRgba16Float;
        header.reserved0 = 0;
        header.reserved1 = 0;
        output.write(reinterpret_cast<const char*>(&header), sizeof(header));
        const std::streamsize bytes =
            static_cast<std::streamsize>(width) * height * 8;
        output.write(reinterpret_cast<const char*>(rgba16), bytes);
        if (!output)
        {
            error = "failed while writing " + path.generic_string();
            return false;
        }
        return true;
    }

    inline bool LoadRlHdrFile(const std::filesystem::path& path, RlHdrImage& image, std::string& error)
    {
        error.clear();
        image = {};
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        if (!input)
        {
            error = "failed to open " + path.generic_string();
            return false;
        }
        const auto size = static_cast<uint64_t>(input.tellg());
        input.seekg(0);
        RlHdrHeader header{};
        input.read(reinterpret_cast<char*>(&header), sizeof(header));
        if (!input)
        {
            error = "rlhdr header truncated";
            return false;
        }
        if (std::memcmp(header.magic, kRlHdrMagic, 8) != 0)
        {
            error = "rlhdr magic mismatch";
            return false;
        }
        if (header.version != kRlHdrVersion || header.format != kRlHdrFormatRgba16Float ||
            header.reserved0 != 0 || header.reserved1 != 0)
        {
            error = "rlhdr version/format/reserved mismatch";
            return false;
        }
        if (header.width == 0 || header.height == 0)
        {
            error = "rlhdr has zero size";
            return false;
        }
        const uint64_t expected =
            kRlHdrHeaderBytes + static_cast<uint64_t>(header.width) * header.height * 8ull;
        if (size != expected)
        {
            error = "rlhdr size mismatch";
            return false;
        }
        image.width = header.width;
        image.height = header.height;
        image.rgba16.resize(static_cast<size_t>(header.width) * header.height * 4u);
        input.read(
            reinterpret_cast<char*>(image.rgba16.data()),
            static_cast<std::streamsize>(image.rgba16.size() * sizeof(uint16_t)));
        if (!input)
        {
            error = "rlhdr pixels truncated";
            return false;
        }
        return true;
    }
}
```

- [ ] **Step 4: Rebuild and run CPU tests**

```powershell
cmake --build --preset windows-debug --parallel --target RenderLabDataContractTests
.\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
```

Expected: `0 failure(s)`. Existing S1.6 tests still pass.

- [ ] **Step 5: Commit**

```bash
git add src/renderer/HdrDump.h src/CMakeLists.txt tests/test_hdr_compare.cpp tests/CMakeLists.txt tests/test_renderer_data.cpp
git commit -m "test: add S2.4 rlhdr dump format and finite-pixel scan"
```

---

### Task 2: Mixed abs/rel compare and HDR metadata identity

**Files:**
- Create: `tests/hdr_compare.h`
- Create: `tests/hdr_compare.cpp`
- Modify: `tests/test_hdr_compare.cpp` (add compare/metadata cases)
- Modify: `tests/CMakeLists.txt` (add `hdr_compare.cpp` / `hdr_compare.h` to both test exe and GoldenCompare)

Numeric **production** tolerances stay as named constants that Task 9 overwrites after the first GPU capture. CPU tests pass their own `absTol`/`relTol` into `CompareHdrRgb` and do not depend on those production numbers.

- [ ] **Step 1: Write failing compare tests** (append inside `RunHdrCompareTests` before `return g_failures`)

```cpp
    #include "hdr_compare.h"
    using namespace renderlab::hdrgolden;

    HdrRgbImage a;
    a.width = 2;
    a.height = 1;
    a.rgb = {0.f, 0.f, 0.f, 1.f, 2.f, 3.f};
    HdrRgbImage b = a;
    HdrCompareStats stats = CompareHdrRgb(a, b, 1e-3f, 1e-2f, 1e-4f);
    Check(stats.mismatchCount == 0 && stats.mae == 0.0, "Identical HDR images match");

    b.rgb[3] = 1.5f;
    stats = CompareHdrRgb(a, b, 1e-3f, 1e-2f, 1e-4f);
    Check(stats.mismatchCount == 1, "Channel that exceeds abs and rel mismatches");

    HdrRgbImage brightRef;
    brightRef.width = 1;
    brightRef.height = 1;
    brightRef.rgb = {10.f, 10.f, 10.f};
    HdrRgbImage brightCand = brightRef;
    brightCand.rgb[0] = 10.05f; // abs 0.05, rel 0.005; absTol 1e-3 and relTol 1e-2 => 0.05 < 0.1, no mismatch
    stats = CompareHdrRgb(brightCand, brightRef, 1e-3f, 1e-2f, 1e-4f);
    Check(stats.mismatchCount == 0, "Small relative error on a bright pixel is tolerated");

    HdrRgbImage nanImg = a;
    nanImg.rgb[0] = std::numeric_limits<float>::quiet_NaN();
    Check(HdrRgbHasNonFinite(nanImg), "NaN RGB is non-finite");
    Check(!HdrRgbHasNonFinite(a), "finite RGB is finite");

    const std::filesystem::path metaDir =
        std::filesystem::temp_directory_path() / "renderlab-s24-meta";
    std::filesystem::create_directories(metaDir);
    {
        std::ofstream json(metaDir / kHdrMetadataFileName, std::ios::binary | std::ios::trunc);
        json << "{\n"
             << "  \"schema\": \"renderlab-hdr-capture-metadata/v1\",\n"
             << "  \"step\": \"S2.4\",\n"
             << "  \"sceneId\": \"cesium-milk-truck\",\n"
             << "  \"cameraPreset\": \"s04-default\",\n"
             << "  \"width\": 1280,\n"
             << "  \"height\": 720,\n"
             << "  \"frameIndex\": 1,\n"
             << "  \"sampleCount\": 1,\n"
             << "  \"verifyLights\": false,\n"
             << "  \"adapterName\": \"Test Adapter\",\n"
             << "  \"driverVersion\": \"1.0\",\n"
             << "  \"hdrFileName\": \"hdr-scene-color.rlhdr\",\n"
             << "  \"diagnosticFileName\": \"lighting-lit.png\",\n"
             << "  \"nonFiniteCount\": 0,\n"
             << "  \"pixelCount\": 921600,\n"
             << "  \"timestampValid\": false\n"
             << "}\n";
    }
    HdrCaptureIdentity identity;
    Check(LoadHdrCaptureIdentity(metaDir, identity, error), "Valid HDR metadata loads");
    Check(IdentityMatchesLockedHdrCapture(identity, error), "Locked identity matches S2.4 constants");

    {
        std::ofstream json(metaDir / kHdrMetadataFileName, std::ios::binary | std::ios::trunc);
        json << "{ \"schema\": \"renderlab-capture-metadata/v1\", \"width\": 1280 }\n";
    }
    Check(!LoadHdrCaptureIdentity(metaDir, identity, error), "S1.6 schema is rejected");
```

Add `#include <fstream>` and `#include <limits>` to the test file.

- [ ] **Step 2: Build to see missing `hdr_compare.h`**

```powershell
cmake --build --preset windows-debug --parallel --target RenderLabDataContractTests
```

Expected: compile error.

- [ ] **Step 3: Implement `tests/hdr_compare.h` and `tests/hdr_compare.cpp`**

`tests/hdr_compare.h`:

```cpp
#pragma once

#include "renderer/HdrDump.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace renderlab::hdrgolden
{
    inline constexpr uint32_t kWidth = 1280;
    inline constexpr uint32_t kHeight = 720;
    inline constexpr uint32_t kFrameIndex = 1;
    inline constexpr uint32_t kSampleCount = 1;
    inline constexpr const char* kSceneId = "cesium-milk-truck";
    inline constexpr const char* kCameraPreset = "s04-default";
    inline constexpr const char* kSchema = "renderlab-hdr-capture-metadata/v1";
    inline constexpr const char* kHdrFileName = "hdr-scene-color.rlhdr";
    inline constexpr const char* kDiagnosticFileName = "lighting-lit.png";
    inline constexpr const char* kHdrMetadataFileName = "hdr-capture-metadata.json";
    inline constexpr const char* kReportFileName = "compare-report.json";
    inline constexpr const char* kRelativeDirectory = "cesium-milk-truck/s04-default/1280x720";
    inline constexpr float kRelativeFloor = 1e-4f;

    // Production tolerances. Task 9 overwrites these from the first approved capture.
    inline constexpr float kAbsTol = 1e-3f;
    inline constexpr float kRelTol = 1e-2f;
    inline constexpr double kMaxMae = 1e-3;
    inline constexpr double kMaxMismatchFraction = 0.002;
    inline constexpr float kPortabilityAbsTol = 1e-2f;
    inline constexpr float kPortabilityRelTol = 5e-2f;
    inline constexpr double kPortabilityMaxMae = 1e-2;
    inline constexpr double kPortabilityMaxMismatchFraction = 0.02;

    struct HdrRgbImage
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<float> rgb;
    };

    struct HdrCompareStats
    {
        double mae = 0.0;
        uint64_t mismatchCount = 0;
        uint64_t pixelCount = 0;
        double mismatchFraction = 0.0;
        bool dimensionMismatch = false;
    };

    enum class Verdict
    {
        Pass,
        Regression,
        Portability,
        Error
    };

    struct HdrCaptureIdentity
    {
        std::string schema;
        std::string sceneId;
        std::string cameraPreset;
        std::string adapterName;
        std::string driverVersion;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t frameIndex = 0;
        uint32_t sampleCount = 0;
        bool verifyLights = true;
        bool hasMetadata = false;
        bool timestampValid = false;
    };

    struct HdrDirectoryCompareResult
    {
        Verdict verdict = Verdict::Error;
        bool adapterMatches = true;
        bool identityMatches = true;
        HdrCaptureIdentity reference;
        HdrCaptureIdentity candidate;
        HdrCompareStats tight;
        HdrCompareStats portability;
        bool passedTight = false;
        bool passedPortability = false;
        std::string summary;
    };

    bool DecodeRlHdrToRgb(const RlHdrImage& image, HdrRgbImage& rgb, std::string& error);
    bool HdrRgbHasNonFinite(const HdrRgbImage& image);
    HdrCompareStats CompareHdrRgb(
        const HdrRgbImage& candidate,
        const HdrRgbImage& reference,
        float absTol,
        float relTol,
        float relativeFloor);
    bool StatsPass(const HdrCompareStats& stats, double maxMae, double maxMismatchFraction);

    bool LoadHdrCaptureIdentity(
        const std::filesystem::path& directory,
        HdrCaptureIdentity& identity,
        std::string& error);
    bool IdentityMatchesLockedHdrCapture(const HdrCaptureIdentity& identity, std::string& error);
    bool HdrIdentitiesCompatible(
        const HdrCaptureIdentity& candidate,
        const HdrCaptureIdentity& reference,
        std::string& error);
    bool HdrAdaptersMatch(const HdrCaptureIdentity& candidate, const HdrCaptureIdentity& reference);

    HdrDirectoryCompareResult CompareHdrDirectories(
        const std::filesystem::path& candidateDir,
        const std::filesystem::path& referenceDir);

    std::string FormatHdrReport(const HdrDirectoryCompareResult& result);
    bool WriteHdrReport(
        const std::filesystem::path& path,
        const HdrDirectoryCompareResult& result,
        std::string& error);
    const char* HdrVerdictName(Verdict verdict);
    int HdrVerdictExitCode(Verdict verdict);
}
```

Implement `tests/hdr_compare.cpp` following `tests/image_compare.cpp` JSON strictness:

- `LoadHdrCaptureIdentity` reads `hdr-capture-metadata.json` with jsoncpp `CharReader`, rejects trailing garbage, requires string keys `schema`, `sceneId`, `cameraPreset`, `adapterName`, `driverVersion`, `hdrFileName`, `diagnosticFileName`; uint32 keys `width`, `height`, `frameIndex`, `sampleCount`, `nonFiniteCount`, `pixelCount` (reject `1280.0`); bool `verifyLights`, `timestampValid`.
- Reject `schema != renderlab-hdr-capture-metadata/v1`.
- `DecodeRlHdrToRgb` converts each texel R,G,B via `HalfToFloat`. If any RGB is non-finite, set error `non-finite RGB` and return false.
- `CompareHdrRgb`: if sizes differ, `dimensionMismatch = true` and return. Else for each pixel/channel apply the contract formula with the passed tols. `mae` is mean abs error over all pixels and 3 channels.
- `CompareHdrDirectories`: load both `.rlhdr` and both metadata files. Missing files / invalid JSON / non-finite / size ≠ 1280×720 → `Error`. Identity not matching locked constants or `verifyLights != false` → `Regression`. Then compare pixels with tight tols; if fail and adapters differ, retry portability tols.
- Do **not** load `lighting-lit.png`.
- Exit codes: pass 0, regression 1, portability 2, error 3.

Mirror `image_compare.cpp` helper `ReadRequiredString` / `ReadRequiredUint32` locally (copy, do not share with S1.6 to avoid coupling schemas).

Add `hdr_compare.cpp` and `hdr_compare.h` to **both** `RenderLabDataContractTests` and `RenderLabGoldenCompare` in `tests/CMakeLists.txt`. Give GoldenCompare:

```cmake
target_include_directories(RenderLabGoldenCompare PRIVATE "${CMAKE_SOURCE_DIR}/src")
```

`RenderLabDataContractTests` already gets `src` via `RenderLab::Renderer`.

- [ ] **Step 4: Rebuild and run**

```powershell
cmake --build --preset windows-debug --parallel --target RenderLabDataContractTests
.\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
```

Expected: `0 failure(s)`.

- [ ] **Step 5: Commit**

```bash
git add tests/hdr_compare.h tests/hdr_compare.cpp tests/test_hdr_compare.cpp tests/CMakeLists.txt
git commit -m "test: add S2.4 HDR compare rule and metadata identity"
```

---

### Task 3: `RenderLabGoldenCompare --mode hdr`

**Files:**
- Modify: `tests/golden_compare_main.cpp`

- [ ] **Step 1: Extend usage and parse `--mode`**

Default mode remains GBuffer (current behavior when `--mode` is omitted). `--mode hdr` selects HDR. Unknown mode exits 3.

Replace `PrintUsage` with:

```cpp
        std::printf(
            "Usage: RenderLabGoldenCompare [--mode gbuffer|hdr] --candidate <dir> --reference <dir> [--channel-swap]\n"
            "\n"
            "Default --mode is gbuffer (S1.6 PNG goldens).\n"
            "--mode hdr compares hdr-scene-color.rlhdr using docs/hdr-regression.md.\n"
            "Exit codes: 0 pass, 1 regression, 2 portability, 3 error.\n"
            "gbuffer --channel-swap: candidate roughness vs reference base-color (must fail).\n"
            "hdr --channel-swap: swap candidate R/B after decode and compare (must fail).\n");
```

Parse `--mode` the same way as `--candidate`. Store `std::string mode = "gbuffer"`. Accept `gbuffer` and `hdr` only.

- [ ] **Step 2: Implement HDR directory compare and channel-swap**

When `mode == "hdr"` and not channel-swap:

```cpp
    #include "hdr_compare.h"
    using namespace renderlab::hdrgolden;
    const HdrDirectoryCompareResult result = CompareHdrDirectories(candidate, reference);
    const std::string report = FormatHdrReport(result);
    std::fputs(report.c_str(), stdout);
    std::string reportError;
    WriteHdrReport(candidate / kReportFileName, result, reportError);
    std::printf("verdict=%s\n", HdrVerdictName(result.verdict));
    return HdrVerdictExitCode(result.verdict);
```

When `mode == "hdr"` and `--channel-swap`:

1. `LoadRlHdrFile` candidate and reference `kHdrFileName`.
2. `DecodeRlHdrToRgb` both.
3. Swap candidate `rgb[i]` with `rgb[i+2]` for each pixel.
4. `CompareHdrRgb` with tight production tols.
5. Print mae/mismatch. Return 0 if `!StatsPass` (failed as expected), else 1.

Keep the existing GBuffer branch unchanged for `mode == "gbuffer"`.

- [ ] **Step 3: Build and smoke `--help`**

```powershell
cmake --build --preset windows-debug --parallel --target RenderLabGoldenCompare
.\out\build\windows-vs2022\bin\Debug\RenderLabGoldenCompare.exe --help
.\out\build\windows-vs2022\bin\Debug\RenderLabGoldenCompare.exe --mode hdr --candidate missing --reference missing
```

Expected: help mentions `--mode hdr`. Missing dirs exit 3.

Pointing `--mode hdr` at `tests/golden/cesium-milk-truck/s04-default/1280x720` must exit 3 (no `.rlhdr` / wrong metadata).

- [ ] **Step 4: Commit**

```bash
git add tests/golden_compare_main.cpp tests/CMakeLists.txt
git commit -m "feat: add GoldenCompare --mode hdr"
```

---

### Task 4: `--output-hdr` CLI

**Files:**
- Modify: `src/app/main.cpp`

- [ ] **Step 1: Add the option and exclusivity**

In `CommandLineOptions` add `std::optional<std::string> outputHdr;`.

In `PrintUsage` add:

```text
  --output-hdr <dir>  S2.4 HDR golden capture: hdr-scene-color.rlhdr, lighting-lit.png,
                      and hdr-capture-metadata.json. Implies --lock-camera and disables
                      the windowed --frames resize/minimize probe. Exclusive with --output.
```

Parse `--output-hdr` like `--output`. After the existing `--gbuffer-view` / `--lighting-view` exclusivity check, add:

```cpp
    if (options.output.has_value() && options.outputHdr.has_value())
    {
        std::fprintf(stderr, "error: --output and --output-hdr are mutually exclusive.\n");
        return 2;
    }
```

- [ ] **Step 2: Treat HDR capture as a dump that disables the resize probe**

```cpp
    const bool hdrOutput = options.outputHdr.has_value();
    const bool dumpGBuffer = options.dumpGBufferViews.has_value() || goldenOutput;
    const bool dumpLighting = options.dumpLightingViews.has_value();
    const bool dumpViews = dumpGBuffer || dumpLighting || hdrOutput;
```

`launchOptions.lockCamera` already follows `dumpViews`. Add to `AppLaunchOptions`:

```cpp
        std::string hdrOutputDirectory;
        bool writeHdrCaptureMetadata = false;
```

Set them when `hdrOutput` is true. Do **not** set `writeCaptureMetadata` or GBuffer dump directories from `--output-hdr`.

- [ ] **Step 3: Verify parse without needing a successful GPU run**

```powershell
cmake --build --preset windows-debug --parallel --target RenderLab
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --help
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --output results\a --output-hdr results\b
```

Expected: help lists `--output-hdr`. Combined flags print the exclusivity error and exit 2 (before a long GPU session).

- [ ] **Step 4: Commit**

```bash
git add src/app/main.cpp src/app/RenderingLabApp.h
git commit -m "feat: parse --output-hdr exclusive of S1.6 --output"
```

(`RenderingLabApp.h` fields can land in this commit even if dump is still a stub that returns false — if you stub, log `"S2.4 HDR dump is not implemented"`.)

---

### Task 5: GPU readback, finite abort, write `.rlhdr`

**Files:**
- Modify: `src/app/RenderingLabApp.h`
- Modify: `src/app/RenderingLabApp.cpp`
- Modify: `src/app/main.cpp` (`afterPresent` hook)

Follow Donut `SaveTextureToFile` staging, but **do not blit to sRGB8**. Copy `HDRSceneColor` as `RGBA16_FLOAT`.

- [ ] **Step 1: Add `DumpHdrCapture`**

In `RenderingLabApp.h`:

```cpp
        bool DumpHdrCapture(const std::string& directory);
        bool WriteHdrCaptureMetadata(const std::string& directory, uint32_t frameIndex, uint64_t nonFiniteCount) const;
```

Add HUD flags or reuse a simple `bool m_hdrDumpCompleted / m_hdrDumpSucceeded` on the app (do not overload GBuffer dump HUD).

Implement `DumpHdrCapture`:

1. If `directory.empty()` or HDR target invalid → error, return false.
2. `create_directories`.
3. `device->waitForIdle()`.
4. Open a command list; if HDR state is tracked as RenderTarget, begin tracking; `copyTexture` to a staging texture created with `createStagingTexture(hdr->getDesc(), CpuAccessMode::Read)`; execute; map.
5. Pack rows into a `std::vector<uint16_t>` of size `width*height*4`. Staging `rowPitch` may be larger than `width*8`; copy `width*8` bytes per row only.
6. `nonFinite = CountNonFiniteRgb(...)`. If `nonFinite > 0`, unmap, log `HDRSceneColor has %llu non-finite RGB texels`, return false **without writing any files**.
7. Write `directory/hdr-scene-color.rlhdr` via `WriteRlHdrFile`.
8. Unmap staging.

Include `renderer/HdrDump.h`.

- [ ] **Step 2: Hook `afterPresent` in `main.cpp`**

Capture `hdrOutput`, `hdrDirectory` in the lambda. After the lighting dump block:

```cpp
                    if (hdrOutput && !app.HdrDumpCompleted())
                    {
                        if (!app.DumpHdrCapture(hdrDirectory))
                        {
                            dumpFailed = true;
                            glfwSetWindowShouldClose(manager.GetWindow(), GLFW_TRUE);
                            return;
                        }
                    }
```

`dumpFailed` / process exit 1 already covers dump failure. After the message loop, if `hdrOutput && !app.HdrDumpSucceeded()` set `exitCode = 1`.

PNG and metadata can still be missing in this task; dump is successful if `.rlhdr` exists. Task 6 adds the other two files to the same function **before** considering success (spec: all three files on success). Prefer implementing the finite abort + rlhdr now and adding PNG/metadata in Task 6 in the same function so a Task 5-only capture is an intermediate state. If you ship Task 5 without PNG, do not run `golden-hdr.ps1` yet.

- [ ] **Step 3: GPU smoke**

```powershell
cmake --build --preset windows-debug --parallel --target RenderLab
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --lock-camera --output-hdr results\s24-task5
```

Expected: exit 0, `results\s24-task5\hdr-scene-color.rlhdr` exists, size `7372832`. No NaN abort on Milk Truck default lights.

- [ ] **Step 4: Commit**

```bash
git add src/app/RenderingLabApp.cpp src/app/RenderingLabApp.h src/app/main.cpp
git commit -m "feat: read back HDRSceneColor and write .rlhdr with finite abort"
```

---

### Task 6: Reinhard PNG, HDR metadata, atomic success

**Files:**
- Modify: `src/app/RenderingLabApp.cpp` (`DumpHdrCapture`, `WriteHdrCaptureMetadata`)
- Modify: `src/app/main.cpp` if the dump hook needs the extra writes

- [ ] **Step 1: Write `lighting-lit.png` inside `DumpHdrCapture` after a finite `.rlhdr`**

Reuse the lighting debug dump target: execute `LightingDebugMode::Lit` into `m_lightingDebugPass.GetOrCreateDumpTarget`, then `engine::SaveTextureToFile(..., lighting-lit.png, false)` as in `DumpLightingDebugViews`. Do not dump world-position or ndotl into the HDR directory.

If PNG write fails, delete the `.rlhdr` already written (spec: a successful capture is all three files; a failed capture must not leave a half-golden). Then return false.

- [ ] **Step 2: `WriteHdrCaptureMetadata`**

Copy the JSON style of `WriteCaptureMetadata`, but schema `renderlab-hdr-capture-metadata/v1`, filename `hdr-capture-metadata.json`, fields from the contract. `verifyLights` from `m_options.verifyLights` (golden path uses default lights / false). `nonFiniteCount` is 0 if this function runs. `pixelCount` = width*height. `timestampValid` and `deferredLightingGpuTimeMilliseconds` from `m_deferredLightingPass.GetHud()`: if `timestampValid`, write the float milliseconds; else omit the time key.

If metadata write fails, delete `.rlhdr` and `lighting-lit.png`.

- [ ] **Step 3: GPU smoke**

```powershell
cmake --build --preset windows-debug --parallel --target RenderLab
.\out\build\windows-vs2022\bin\Debug\RenderLab.exe --headless --lock-camera --output-hdr results\s24-task6
```

Expected files:

- `hdr-scene-color.rlhdr`
- `lighting-lit.png`
- `hdr-capture-metadata.json` with `"verifyLights": false`, `"schema": "renderlab-hdr-capture-metadata/v1"`, `"nonFiniteCount": 0`

`--output` still writes only GBuffer files (run `scripts\golden.ps1 -Mode Compare` against an existing S1.6 candidate if you have one, or at least `--help` + a GBuffer `--output` smoke).

- [ ] **Step 4: Commit**

```bash
git add src/app/RenderingLabApp.cpp src/app/RenderingLabApp.h src/app/main.cpp
git commit -m "feat: write HDR capture PNG thumbnail and metadata"
```

---

### Task 7: `scripts/golden-hdr.ps1`

**Files:**
- Create: `scripts/golden-hdr.ps1`

Copy `scripts/golden.ps1` structure. Differences:

- `$goldenDir = Join-Path $repoRoot 'tests\golden-hdr\cesium-milk-truck\s04-default\1280x720'`
- Capture: `& $exe --headless --lock-camera --output-hdr $OutputDir`
- Required files: `hdr-scene-color.rlhdr`, `lighting-lit.png`, `hdr-capture-metadata.json`
- Compare: `RenderLabGoldenCompare.exe --mode hdr --candidate ... --reference $goldenDir`
- Swap: `--mode hdr --channel-swap`
- Default results dirs: `results\s24-$config-run1` / `run2`
- Do not call `golden.ps1`

`Verify` may fail until Task 9 commits goldens. That is expected. `Capture` must work now.

- [ ] **Step 1: Write the script** (full file, same param block as `golden.ps1` with Capture/Compare/Swap/Verify).

- [ ] **Step 2: Run Capture**

```powershell
powershell -NoProfile -File scripts\golden-hdr.ps1 -Mode Capture -Candidate results\s24-script
```

Expected: three files under `results\s24-script`.

- [ ] **Step 3: Commit**

```bash
git add scripts/golden-hdr.ps1
git commit -m "feat: add golden-hdr.ps1 capture and compare harness"
```

---

### Task 8: README / help / capture-guide pointers for the live flag

**Files:**
- Modify: `README.md` (command table `--output-hdr` row; do not claim goldens exist yet)
- Modify: `docs/capture-guide.md` (S2.4 dump command)
- Modify: `docs/hdr-regression.md` status line still “implementation in progress” until Task 10

- [ ] **Step 1: Add the README row next to `--output`:**

`|--output-hdr <dir> | S2.4 HDR golden capture: hdr-scene-color.rlhdr, lighting-lit.png, hdr-capture-metadata.json; implies --lock-camera; exclusive with --output; disables the resize probe |`

Add `powershell -NoProfile -File scripts\golden-hdr.ps1` under Run, with a note that Verify needs the committed HDR golden (Task 9).

- [ ] **Step 2: Commit**

```bash
git add README.md docs/capture-guide.md
git commit -m "docs: document --output-hdr and golden-hdr.ps1"
```

---

### Task 9: First approved capture, freeze tolerances, commit goldens

**Files:**
- Create: `tests/golden-hdr/cesium-milk-truck/s04-default/1280x720/hdr-scene-color.rlhdr`
- Create: `tests/golden-hdr/cesium-milk-truck/s04-default/1280x720/lighting-lit.png`
- Create: `tests/golden-hdr/cesium-milk-truck/s04-default/1280x720/hdr-capture-metadata.json`
- Create: `tests/golden-hdr/cesium-milk-truck/s04-default/1280x720/manifest.json`
- Create: `tests/golden-hdr/README.md`
- Modify: `tests/hdr_compare.h` (replace the Task 2 placeholder tols)
- Modify: `docs/hdr-regression.md` (fill the tolerance table; status **frozen after S2.4 capture**)
- Modify: `tests/test_hdr_compare.cpp` (self-compare committed golden after it exists, like S1.6)

Do this on the implementing GPU. That adapter becomes the approved HDR baseline.

- [ ] **Step 1: Capture twice**

```powershell
cmake --build --preset windows-debug --parallel
powershell -NoProfile -File scripts\golden-hdr.ps1 -Mode Capture -Candidate results\s24-approve-a
powershell -NoProfile -File scripts\golden-hdr.ps1 -Mode Capture -Candidate results\s24-approve-b
```

- [ ] **Step 2: Measure a-vs-b and freeze tols**

Temporarily point `--reference` at `results\s24-approve-a` and `--candidate` at `results\s24-approve-b` (copy a into a fake golden tree **or** pass those dirs directly to GoldenCompare). Read `mae` / `mismatchFraction` from the report.

Set production constants in `tests/hdr_compare.h`:

- Tight `absTol` / `relTol` / `maxMae` / `maxMismatchFraction` at least **2×** the observed a-vs-b figures, and not tighter than `1e-4` abs / `1e-3` rel if the two captures were bit-identical (mae 0).
- Portability band **10×** tight MAE and mismatch, abs/rel at least **5×** tight, matching the S1.6 idea that a different adapter is not an automatic fail.

Write the same numbers into `docs/hdr-regression.md` section 6 and `tests/golden-hdr/.../manifest.json`.

- [ ] **Step 3: Install the golden**

Copy `results\s24-approve-a\*` into `tests/golden-hdr/cesium-milk-truck/s04-default/1280x720/`. Do not copy `compare-report.json`. Add `tests/golden-hdr/README.md` stating this is the S2.4 HDR baseline and the contract is `docs/hdr-regression.md`.

Add a CPU self-compare in `test_hdr_compare.cpp` that loads the committed golden against itself (skip if the directory is missing, but after this task it exists). Expect pass.

- [ ] **Step 4: Verify**

```powershell
cmake --build --preset windows-debug --parallel
.\out\build\windows-vs2022\bin\Debug\RenderLabDataContractTests.exe
powershell -NoProfile -File scripts\golden-hdr.ps1 -Mode Verify -Configuration Debug
powershell -NoProfile -File scripts\golden.ps1 -Mode Verify -Configuration Debug
```

Expected: HDR Verify passes (two captures + R/B swap fails). S1.6 Verify still passes.

- [ ] **Step 5: Commit**

```bash
git add tests/golden-hdr tests/hdr_compare.h tests/test_hdr_compare.cpp docs/hdr-regression.md
git commit -m "test: commit S2.4 HDR golden and freeze compare tolerances"
```

---

### Task 10: PIX evidence and step completion docs

**Files:**
- Modify: `docs/PROGRESS.md` (S2.4 completion block in the existing template)
- Modify: `docs/capture-guide.md` (S2.4 PIX bullets from the contract)
- Modify: `README.md` (S2.4 complete; next is S3.1)
- Modify: `IMPLEMENTATION_PLAN.md` current-progress sentence
- Modify: `docs/lighting.md` status (S2.4 implemented)
- Modify: `docs/hdr-regression.md` status **frozen after S2.4**

- [ ] **Step 1: PIX**

Capture one `--lock-camera` frame (default present `lit`). Confirm under `Render`: `GBuffer` then `DeferredLighting` writes `HDRSceneColor`; declared GBuffer SRVs; no lighting UAV; no second HDR target. Record adapter/driver and the nesting in `PROGRESS.md`. Do not commit the `.wpix`.

- [ ] **Step 2: Fill `docs/PROGRESS.md` S2.4 evidence** using the existing template (commands, Debug/Release DataContractTests, `golden-hdr.ps1 Verify`, `golden.ps1 Verify`, PIX, artifacts, known limitations: no tone map, Milk Truck metallic weak oracle, `--output` still GBuffer-only). Next step: **S3.1**.

- [ ] **Step 3: Commit**

```bash
git add docs/PROGRESS.md docs/capture-guide.md docs/lighting.md docs/hdr-regression.md README.md IMPLEMENTATION_PLAN.md
git commit -m "docs: record S2.4 HDR regression completion"
```

Then a follow-up docs commit with the completion hash if that is still the repo convention (`docs: record S2.3 completion commit hash` style).

---

## Self-review

**Spec coverage**

| Contract section | Task |
|---|---|
| Locked Milk Truck / default lights / frame 1 | 4, 5, 9 |
| `--output-hdr` exclusive, no resize probe | 4 |
| `.rlhdr` layout | 1 |
| Finite abort, no files | 1, 5 |
| Reinhard PNG committed, not compared | 6, 9 |
| Metadata schema / best-effort GPU time | 6, 2 |
| Mixed abs/rel + portability + exit codes | 2, 3, 9 |
| `--mode hdr` / channel-swap | 3, 7 |
| `golden-hdr.ps1` | 7, 9 |
| CPU tests, no BRDF port | 1–2 |
| CI = CPU only | 9 (Verify is local) |
| PIX evidence | 10 |
| Do not extend `--output` / `golden.ps1` | 4, 7, 9 check |

**Placeholder scan:** Production tols start as named constants in Task 2 and are replaced with measured values in Task 9. That is sequenced work, not an open spec hole.

**Type consistency:** `kHdrMetadataFileName`, `kHdrFileName`, `DumpHdrCapture`, `CompareHdrDirectories`, `--output-hdr`, `--mode hdr` are used with the same names in later tasks.
