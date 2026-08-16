#include "gfx/d3d12/adapter_report.h"

#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string_view>

namespace renderlab::gfx::d3d12
{
namespace
{
void AppendHex32(std::wostringstream &stream, std::uint32_t value)
{
    stream << L"0x" << std::uppercase << std::hex << std::setfill(L'0') << std::setw(8)
           << value << std::dec;
}

void AppendHex16(std::wostringstream &stream, std::uint32_t value)
{
    stream << L"0x" << std::uppercase << std::hex << std::setfill(L'0') << std::setw(4)
           << value << std::dec;
}

void AppendStatus(std::wostringstream &stream, HRESULT status)
{
    AppendHex32(stream, static_cast<std::uint32_t>(status));
}

void AppendDriverVersion(std::wostringstream &stream, const LARGE_INTEGER &version)
{
    const auto high = static_cast<std::uint32_t>(version.HighPart);
    const auto low = static_cast<std::uint32_t>(version.LowPart);

    stream << ((high >> 16) & 0xFFFF) << L'.' << (high & 0xFFFF) << L'.'
           << ((low >> 16) & 0xFFFF) << L'.' << (low & 0xFFFF);
}

std::wstring_view BooleanText(bool value) noexcept
{
    return value ? L"true" : L"false";
}
} // namespace

std::wstring FormatAdapterReport(const AdapterDiscoveryResult &discovery)
{
    std::wostringstream report;
    report << L"D3D12 adapters: enumeration_status=";
    AppendStatus(report, discovery.status);
    report << L"; enumerated_count=" << discovery.adapters.size() << L"; selected_index=";

    if (discovery.selectedIndex.has_value())
    {
        report << *discovery.selectedIndex << L'\n';
        report << L"selection_reason=first hardware adapter supporting D3D12 in DXGI "
                  L"high-performance order\n";
    }
    else
    {
        report << L"none\n";
        report << L"selection_reason=no hardware adapter supporting D3D12\n";
    }

    for (std::size_t index = 0; index < discovery.adapters.size(); ++index)
    {
        const AdapterInfo &adapter = discovery.adapters[index];
        const bool selected =
            discovery.selectedIndex.has_value() && *discovery.selectedIndex == index;

        report << L"Adapter[" << index << L"]:\n";
        report << L"  description=" << adapter.properties.Description << L'\n';
        report << L"  preference_index=" << adapter.preferenceIndex << L'\n';
        report << L"  vendor_id=";
        AppendHex16(report, adapter.properties.VendorId);
        report << L'\n';
        report << L"  device_id=";
        AppendHex16(report, adapter.properties.DeviceId);
        report << L'\n';
        report << L"  luid=";
        AppendHex32(report, static_cast<std::uint32_t>(adapter.properties.AdapterLuid.HighPart));
        report << L':';
        AppendHex32(report, adapter.properties.AdapterLuid.LowPart);
        report << L'\n';
        report << L"  dedicated_video_memory_bytes="
               << adapter.properties.DedicatedVideoMemory << L'\n';
        report << L"  software=" << BooleanText(adapter.candidate.isSoftware) << L'\n';
        report << L"  d3d12_probe_status=";
        AppendStatus(report, adapter.probeStatus);
        report << L'\n';
        report << L"  supports_d3d12=" << BooleanText(adapter.candidate.supportsD3D12)
               << L'\n';
        report << L"  driver_version_status=";
        AppendStatus(report, adapter.driverVersionStatus);
        report << L'\n';
        report << L"  driver_version=";
        if (SUCCEEDED(adapter.driverVersionStatus))
        {
            AppendDriverVersion(report, adapter.driverVersion);
        }
        else
        {
            report << L"unavailable";
        }
        report << L'\n';
        report << L"  selected=" << BooleanText(selected) << L'\n';
    }

    return report.str();
}
} // namespace renderlab::gfx::d3d12
