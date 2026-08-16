#include "platform/win32/win32_application.h"

#include <shellapi.h>
#include <windows.h>
#include <wrl/client.h>

#include <exception>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include "gfx/d3d12/adapter_discovery.h"
#include "gfx/d3d12/adapter_report.h"
#include "gfx/d3d12/diagnostics_bootstrap.h"
#include "gfx/d3d12/diagnostics_config.h"
#include "gfx/d3d12/diagnostics_report.h"
#include "gfx/d3d12/dxgi_factory.h"

namespace
{
struct LocalFreeDeleter
{
    void operator()(wchar_t **memory) const noexcept
    {
        LocalFree(memory);
    }
};

struct StartupOptions
{
    bool smokeTest = false;
    bool gpuBasedValidation = false;
};

StartupOptions ParseStartupOptions()
{
    int argumentCount = 0;
    std::unique_ptr<wchar_t *, LocalFreeDeleter> arguments(
        CommandLineToArgvW(GetCommandLineW(), &argumentCount));
    if (!arguments)
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "CommandLineToArgvW");
    }

    StartupOptions options = {};
    for (int index = 1; index < argumentCount; ++index)
    {
        const std::wstring_view argument(arguments.get()[index]);
        if (argument == L"--smoke-test" || argument == L"--smoke-test=window-lifecycle")
        {
            options.smokeTest = true;
        }
        else if (argument == L"--gpu-validation")
        {
            options.gpuBasedValidation = true;
        }
    }

    return options;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    bool smokeTest =
        std::wstring_view(GetCommandLineW()).find(L"--smoke-test") != std::wstring_view::npos;

    try
    {
        const StartupOptions options = ParseStartupOptions();
        smokeTest = options.smokeTest;

        auto diagnosticsConfig =
            renderlab::gfx::d3d12::DefaultDiagnosticsConfig();
        diagnosticsConfig.enableGpuBasedValidation = options.gpuBasedValidation;

        const auto diagnostics =
            renderlab::gfx::d3d12::ConfigureD3D12Diagnostics(diagnosticsConfig);

        const auto diagnosticsReport =
            renderlab::gfx::d3d12::FormatDiagnosticsReport(diagnosticsConfig, diagnostics);

        OutputDebugStringA(diagnosticsReport.c_str());
        OutputDebugStringA("\n");

        if (FAILED(diagnostics.status))
        {
            throw std::system_error(
                static_cast<int>(diagnostics.status),
                std::system_category(),
                "ConfigureD3D12Diagnostics");
        }

        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;

        const HRESULT factoryStatus =
            renderlab::gfx::d3d12::CreateDxgiFactory(
                diagnostics.debugLayerEnabled,
                factory);

        if (FAILED(factoryStatus))
        {
            throw std::system_error(
                static_cast<int>(factoryStatus),
                std::system_category(),
                "CreateDxgiFactory");
        }

        const auto adapterDiscovery =
            renderlab::gfx::d3d12::DiscoverHardwareAdapters(*factory.Get());

        const auto adapterReport =
            renderlab::gfx::d3d12::FormatAdapterReport(adapterDiscovery);

        OutputDebugStringW(adapterReport.c_str());
        OutputDebugStringW(L"\n");

        if (FAILED(adapterDiscovery.status))
        {
            throw std::system_error(
                static_cast<int>(adapterDiscovery.status),
                std::system_category(),
                "DiscoverHardwareAdapters");
        }

        if (!adapterDiscovery.selectedIndex.has_value())
        {
            throw std::runtime_error("No hardware adapter supporting D3D12 was found");
        }

        renderlab::platform::Win32Application application(instance, showCommand, smokeTest);
        return application.Run();
    }
    catch (const std::exception &exception)
    {
        OutputDebugStringA(exception.what());
        OutputDebugStringA("\n");

        if (!smokeTest)
        {
            MessageBoxA(nullptr, exception.what(), "RenderLab startup failure",
                        MB_OK | MB_ICONERROR);
        }

        return 1;
    }
}
