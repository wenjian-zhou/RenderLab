#include "platform/win32/win32_application.h"

#include <shellapi.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "gfx/d3d12/adapter_discovery.h"
#include "gfx/d3d12/adapter_report.h"
#include "gfx/d3d12/depth_stencil_storage.h"
#include "gfx/d3d12/device_bootstrap.h"
#include "gfx/d3d12/device_bootstrap_report.h"
#include "gfx/d3d12/device_feature_report.h"
#include "gfx/d3d12/device_features.h"
#include "gfx/d3d12/device_removal_report.h"
#include "gfx/d3d12/diagnostics_bootstrap.h"
#include "gfx/d3d12/diagnostics_config.h"
#include "gfx/d3d12/diagnostics_report.h"
#include "gfx/d3d12/dxgi_factory.h"
#include "gfx/d3d12/hresult_error.h"
#include "gfx/d3d12/info_queue_messages.h"
#include "gfx/d3d12/info_queue_report.h"
#include "gfx/d3d12/window_swap_chain.h"

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
    bool forceDeviceInitializationFailure = false;
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
        else if (argument == L"--force-device-init-failure")
        {
            options.forceDeviceInitializationFailure = true;
        }
    }

    return options;
}

void OutputDiagnostic(const char *diagnostic, std::size_t length) noexcept
{
    OutputDebugStringA(diagnostic);
    OutputDebugStringA("\n");

    const HANDLE standardError = GetStdHandle(STD_ERROR_HANDLE);
    if (standardError == nullptr || standardError == INVALID_HANDLE_VALUE)
    {
        return;
    }

    DWORD bytesWritten = 0;
    static_cast<void>(WriteFile(
        standardError,
        diagnostic,
        static_cast<DWORD>(length),
        &bytesWritten,
        nullptr));
    static constexpr char newline = '\n';
    static_cast<void>(WriteFile(
        standardError,
        &newline,
        1,
        &bytesWritten,
        nullptr));
}

void OutputDiagnostic(const std::string &diagnostic) noexcept
{
    OutputDiagnostic(diagnostic.c_str(), diagnostic.size());
}

void OutputDiagnostic(const char *diagnostic) noexcept
{
    OutputDiagnostic(diagnostic, std::strlen(diagnostic));
}

renderlab::gfx::d3d12::InfoQueueCollectionResult
CollectAndOutputInfoQueueMessages(ID3D12InfoQueue *infoQueue)
{
    auto collection =
        renderlab::gfx::d3d12::CollectInfoQueueMessages(infoQueue);
    const auto report =
        renderlab::gfx::d3d12::FormatInfoQueueReport(collection);

    OutputDiagnostic(report);
    return collection;
}

void EnforceInfoQueueCollection(
    const renderlab::gfx::d3d12::InfoQueueCollectionResult &collection)
{
    renderlab::gfx::d3d12::ThrowIfFailed(
        collection.status,
        "CollectInfoQueueMessages");

    if (collection.hasRunFailure)
    {
        throw std::runtime_error(
            "D3D12 InfoQueue reported Error or Corruption messages");
    }
}

void ReportDeviceRemovalState(ID3D12Device *device)
{
    const auto report =
        renderlab::gfx::d3d12::FormatDeviceRemovalReport(
            renderlab::gfx::d3d12::CaptureDeviceRemovalReport(device));

    OutputDiagnostic(report);
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

        OutputDiagnostic(diagnosticsReport);

        renderlab::gfx::d3d12::ThrowIfFailed(
            diagnostics.status,
            "ConfigureD3D12Diagnostics");

        Microsoft::WRL::ComPtr<IDXGIFactory6> factory;

        const HRESULT factoryStatus =
            renderlab::gfx::d3d12::CreateDxgiFactory(
                diagnostics.debugLayerEnabled,
                factory);

        renderlab::gfx::d3d12::ThrowIfFailed(
            factoryStatus,
            "CreateDxgiFactory");

        const auto adapterDiscovery =
            renderlab::gfx::d3d12::DiscoverHardwareAdapters(*factory.Get());

        const auto adapterReport =
            renderlab::gfx::d3d12::FormatAdapterReport(adapterDiscovery);

        OutputDebugStringW(adapterReport.c_str());
        OutputDebugStringW(L"\n");

        renderlab::gfx::d3d12::ThrowIfFailed(
            adapterDiscovery.status,
            "DiscoverHardwareAdapters");

        if (!adapterDiscovery.selectedIndex.has_value())
        {
            throw std::runtime_error("No hardware adapter supporting D3D12 was found");
        }

        const std::size_t selectedIndex = *adapterDiscovery.selectedIndex;
        if (selectedIndex >= adapterDiscovery.adapters.size())
        {
            throw std::logic_error("Selected adapter index is out of range");
        }

        const auto &selectedAdapterInfo =
            adapterDiscovery.adapters[selectedIndex];

        auto deviceBootstrap =
            renderlab::gfx::d3d12::CreateDeviceBootstrap(
                selectedAdapterInfo.adapter,
                diagnostics.debugLayerEnabled);

        const auto deviceBootstrapReportData =
            renderlab::gfx::d3d12::MakeDeviceBootstrapReportData(
                deviceBootstrap);

        const auto deviceBootstrapReport =
            renderlab::gfx::d3d12::FormatDeviceBootstrapReport(
                deviceBootstrapReportData);

        OutputDiagnostic(deviceBootstrapReport);

        if (FAILED(deviceBootstrap.status) && deviceBootstrap.device)
        {
            static_cast<void>(CollectAndOutputInfoQueueMessages(
                deviceBootstrap.infoQueue.Get()));

            const auto removalReport =
                renderlab::gfx::d3d12::FormatDeviceRemovalReport(
                    renderlab::gfx::d3d12::CaptureDeviceRemovalReport(
                        deviceBootstrap.device.Get()));
            OutputDiagnostic(removalReport);
        }

        renderlab::gfx::d3d12::ThrowIfFailed(
            deviceBootstrap.status,
            "CreateDeviceBootstrap");

        if (options.forceDeviceInitializationFailure)
        {
            const auto failureMessages =
                CollectAndOutputInfoQueueMessages(
                    deviceBootstrap.infoQueue.Get());
            ReportDeviceRemovalState(deviceBootstrap.device.Get());
            EnforceInfoQueueCollection(failureMessages);
            renderlab::gfx::d3d12::ThrowIfFailed(
                E_FAIL,
                "ForcedDeviceInitializationFailure");
        }

        const auto deviceFeatureQuery =
            renderlab::gfx::d3d12::QueryDeviceFeatures(
                *deviceBootstrap.device.Get());

        const auto deviceFeatureReport =
            renderlab::gfx::d3d12::FormatDeviceFeatureReport(
                deviceFeatureQuery);

        OutputDiagnostic(deviceFeatureReport);

        ReportDeviceRemovalState(deviceBootstrap.device.Get());

        const auto initializationMessages =
            CollectAndOutputInfoQueueMessages(
                deviceBootstrap.infoQueue.Get());

        renderlab::gfx::d3d12::ThrowIfFailed(
            deviceFeatureQuery.status,
            "QueryDeviceFeatures");
        EnforceInfoQueueCollection(initializationMessages);

        renderlab::platform::Win32Application application(instance, showCommand, smokeTest);
        application.Initialize();

        auto windowSwapChain =
            renderlab::gfx::d3d12::CreateWindowSwapChain(
                *factory.Get(),
                *deviceBootstrap.directQueue.Get(),
                application.window());

        renderlab::gfx::d3d12::CreateSwapChainRenderTargetStorage(
            *deviceBootstrap.device.Get(),
            windowSwapChain);

        auto depthStencilStorage =
            renderlab::gfx::d3d12::CreateDepthStencilStorage(
                *deviceBootstrap.device.Get());

        const int applicationResult = application.Run();

        depthStencilStorage.dsvHeap.Reset();

        // Release swap-chain views and resources before the swap chain,
        // Direct queue, and Device so teardown diagnostics observe the full
        // owned lifetime.
        for (auto &backBuffer : windowSwapChain.backBuffers)
        {
            backBuffer.Reset();
        }
        windowSwapChain.rtvHeap.Reset();
        windowSwapChain.swapChain.Reset();

        // Release the queue while the InfoQueue and Device are still alive so
        // teardown diagnostics are included in the final checkpoint.
        deviceBootstrap.directQueue.Reset();

        const auto shutdownMessages =
            CollectAndOutputInfoQueueMessages(
                deviceBootstrap.infoQueue.Get());
        EnforceInfoQueueCollection(shutdownMessages);

        return applicationResult;
    }
    catch (const std::exception &exception)
    {
        OutputDiagnostic(exception.what());

        if (!smokeTest)
        {
            MessageBoxA(nullptr, exception.what(), "RenderLab startup failure",
                        MB_OK | MB_ICONERROR);
        }

        return 1;
    }
}
