#include "gfx/d3d12/adapter_report.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>
#include <utility>

namespace
{
using renderlab::gfx::d3d12::AdapterDiscoveryResult;
using renderlab::gfx::d3d12::AdapterInfo;
using renderlab::gfx::d3d12::FormatAdapterReport;

AdapterInfo MakeAdapter(std::wstring_view description)
{
    AdapterInfo adapter = {};
    const std::size_t length =
        (std::min)(description.size(), std::size(adapter.properties.Description) - 1);
    std::copy_n(description.begin(), length, adapter.properties.Description);
    adapter.properties.Description[length] = L'\0';
    return adapter;
}
} // namespace

TEST_CASE("adapter report describes an empty discovery", "[adapter-report]")
{
    const AdapterDiscoveryResult discovery{};

    const auto report = FormatAdapterReport(discovery);

    REQUIRE(report.find(L"enumerated_count=0") != std::wstring::npos);
    REQUIRE(report.find(L"selected_index=none") != std::wstring::npos);
    REQUIRE(report.find(L"selection_reason=no hardware adapter supporting D3D12") !=
            std::wstring::npos);
}

TEST_CASE("adapter report formats a selected hardware adapter", "[adapter-report]")
{
    AdapterDiscoveryResult discovery{};
    AdapterInfo adapter = MakeAdapter(L"Synthetic GPU");
    adapter.preferenceIndex = 3;
    adapter.properties.VendorId = 0x10DE;
    adapter.properties.DeviceId = 0x2684;
    adapter.properties.AdapterLuid.HighPart = 0x12345678;
    adapter.properties.AdapterLuid.LowPart = 0x9ABCDEF0;
    adapter.properties.DedicatedVideoMemory = 12'884'901'888ULL;
    adapter.candidate.isSoftware = false;
    adapter.candidate.supportsD3D12 = true;
    adapter.probeStatus = S_OK;
    adapter.driverVersionStatus = S_OK;
    adapter.driverVersion.HighPart = static_cast<LONG>((31U << 16) | 0U);
    adapter.driverVersion.LowPart = static_cast<LONG>((15U << 16) | 7688U);
    discovery.adapters.push_back(std::move(adapter));
    discovery.selectedIndex = 0;

    const auto report = FormatAdapterReport(discovery);

    REQUIRE(report.find(L"description=Synthetic GPU") != std::wstring::npos);
    REQUIRE(report.find(L"preference_index=3") != std::wstring::npos);
    REQUIRE(report.find(L"vendor_id=0x10DE") != std::wstring::npos);
    REQUIRE(report.find(L"device_id=0x2684") != std::wstring::npos);
    REQUIRE(report.find(L"luid=0x12345678:0x9ABCDEF0") != std::wstring::npos);
    REQUIRE(report.find(L"dedicated_video_memory_bytes=12884901888") != std::wstring::npos);
    REQUIRE(report.find(L"d3d12_probe_status=0x00000000") != std::wstring::npos);
    REQUIRE(report.find(L"driver_version=31.0.15.7688") != std::wstring::npos);
    REQUIRE(report.find(L"selected=true") != std::wstring::npos);
}

TEST_CASE("adapter report retains rejected adapter details", "[adapter-report]")
{
    AdapterDiscoveryResult discovery{};
    AdapterInfo adapter = MakeAdapter(L"Software Adapter");
    adapter.candidate.isSoftware = true;
    adapter.candidate.supportsD3D12 = false;
    adapter.probeStatus = E_FAIL;
    adapter.driverVersionStatus = E_NOINTERFACE;
    discovery.adapters.push_back(std::move(adapter));

    const auto report = FormatAdapterReport(discovery);

    REQUIRE(report.find(L"software=true") != std::wstring::npos);
    REQUIRE(report.find(L"supports_d3d12=false") != std::wstring::npos);
    REQUIRE(report.find(L"d3d12_probe_status=0x80004005") != std::wstring::npos);
    REQUIRE(report.find(L"driver_version=unavailable") != std::wstring::npos);
    REQUIRE(report.find(L"selected=false") != std::wstring::npos);
}

TEST_CASE("adapter report marks only the selected index", "[adapter-report]")
{
    AdapterDiscoveryResult discovery{};
    discovery.adapters.push_back(MakeAdapter(L"First GPU"));
    discovery.adapters.push_back(MakeAdapter(L"Second GPU"));
    discovery.selectedIndex = 1;

    const auto report = FormatAdapterReport(discovery);

    const auto firstAdapter = report.find(L"Adapter[0]:");
    const auto secondAdapter = report.find(L"Adapter[1]:");
    REQUIRE(firstAdapter != std::wstring::npos);
    REQUIRE(secondAdapter != std::wstring::npos);
    REQUIRE(report.find(L"selected=false", firstAdapter) < secondAdapter);
    REQUIRE(report.find(L"selected=true", secondAdapter) != std::wstring::npos);
}
