#include "FrameMarkers.h"
#include "RenderingLabApp.h"
#include "SceneCatalog.h"

#include <donut/app/DeviceManager.h>
#include <donut/core/log.h>
#include <donut/core/vfs/VFS.h>
#include <donut/engine/ShaderFactory.h>
#include <nvrhi/utils.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace donut;

namespace
{
    constexpr const char* kWindowTitle = "RenderLab";

    struct CommandLineOptions
    {
        std::optional<std::string> scene;
        bool headless = false;
        std::optional<uint32_t> frames;
        std::optional<std::string> output;
        bool lockCamera = false;
        bool help = false;
        std::optional<std::string> gbufferView;
        std::optional<std::string> dumpGBufferViews;
    };

    struct ValidationLog
    {
        std::atomic<int> errorCount{0};
        log::Callback previous;
    };

    void PrintUsage(const char* executable)
    {
        (void)executable;
        std::printf(
            "Usage: RenderLab [options]\n"
            "\n"
            "Options:\n"
            "  --help              Show this help message\n"
            "  --scene <id|path>   Scene id or path relative to scenes/\n"
            "                      Default: cesium-milk-truck\n"
            "                      Fallback: fallback-boxes\n"
            "  --lock-camera       Disable free-camera motion and keep the S0.4 preset\n"
            "  --gbuffer-view <m>  Debug channel: base-color, world-normal, roughness,\n"
            "                      metallic, ao-flags, linear-depth (default: base-color)\n"
            "  --dump-gbuffer-views <dir>\n"
            "                      Dump every mandatory GBuffer debug view as PNG under <dir>\n"
            "                      (visualization dump). Implies --lock-camera.\n"
            "  --headless          CI-safe smoke: hide the window, lock the camera, present a\n"
            "                      fixed frame count (default 8), then exit\n"
            "  --frames <n>        Present n frames, then exit\n"
            "  --output <dir>      S1.6 golden capture: dump every mandatory view plus\n"
            "                      capture-metadata.json under <dir>. Implies --lock-camera\n"
            "                      and disables the windowed --frames resize/minimize probe.\n"
            "                      Locked defaults: cesium-milk-truck, s04-default, 1280x720, frame 1.\n"
            "  --dx12, --d3d12     Select the D3D12 backend (default and only supported API)\n"
            "\n"
            "RenderLab is D3D12-only. Donut DeviceManager owns the window, device, queues,\n"
            "fences, and swap chain. Scene files are loaded through Donut's VFS from scenes/.\n");
    }

    bool EqualsOption(std::string_view value, std::string_view option)
    {
        return value == option;
    }

    bool ParseUInt32(const char* text, uint32_t& value)
    {
        if (!text || *text == '\0')
        {
            return false;
        }

        char* end = nullptr;
        const unsigned long parsed = std::strtoul(text, &end, 10);
        if (end == text || *end != '\0' || parsed == 0 || parsed > UINT32_MAX)
        {
            return false;
        }

        value = static_cast<uint32_t>(parsed);
        return true;
    }

    bool ParseCommandLine(int argc, char** argv, CommandLineOptions& options, std::string& error)
    {
        for (int index = 1; index < argc; ++index)
        {
            const char* argument = argv[index];
            if (EqualsOption(argument, "--help") || EqualsOption(argument, "-h"))
            {
                options.help = true;
                continue;
            }

            if (EqualsOption(argument, "--scene"))
            {
                if (index + 1 >= argc)
                {
                    error = "--scene requires a path argument.";
                    return false;
                }
                options.scene = argv[++index];
                continue;
            }

            if (EqualsOption(argument, "--lock-camera") || EqualsOption(argument, "--no-free-camera"))
            {
                options.lockCamera = true;
                continue;
            }

            if (EqualsOption(argument, "--headless"))
            {
                options.headless = true;
                continue;
            }

            if (EqualsOption(argument, "--frames"))
            {
                if (index + 1 >= argc)
                {
                    error = "--frames requires a positive integer argument.";
                    return false;
                }

                uint32_t frameCount = 0;
                if (!ParseUInt32(argv[index + 1], frameCount))
                {
                    error = "--frames requires a positive integer argument.";
                    return false;
                }
                options.frames = frameCount;
                ++index;
                continue;
            }

            if (EqualsOption(argument, "--output"))
            {
                if (index + 1 >= argc)
                {
                    error = "--output requires a path argument.";
                    return false;
                }
                options.output = argv[++index];
                continue;
            }

            if (EqualsOption(argument, "--gbuffer-view"))
            {
                if (index + 1 >= argc)
                {
                    error = "--gbuffer-view requires a channel name.";
                    return false;
                }
                options.gbufferView = argv[++index];
                continue;
            }

            if (EqualsOption(argument, "--dump-gbuffer-views"))
            {
                if (index + 1 >= argc)
                {
                    error = "--dump-gbuffer-views requires a directory path.";
                    return false;
                }
                options.dumpGBufferViews = argv[++index];
                continue;
            }

            if (EqualsOption(argument, "--dx12") || EqualsOption(argument, "-dx12") ||
                EqualsOption(argument, "--d3d12") || EqualsOption(argument, "-d3d12"))
            {
                continue;
            }

            if (EqualsOption(argument, "--dx11") || EqualsOption(argument, "-dx11") ||
                EqualsOption(argument, "--d3d11") || EqualsOption(argument, "-d3d11") ||
                EqualsOption(argument, "--vk") || EqualsOption(argument, "-vk") ||
                EqualsOption(argument, "--vulkan") || EqualsOption(argument, "-vulkan"))
            {
                error = "RenderLab is D3D12-only. The requested graphics API is not supported.";
                return false;
            }

            error = std::string("Unknown argument: ") + argument;
            return false;
        }

        return true;
    }

    std::filesystem::path FindFrameworkShaderDirectory(nvrhi::GraphicsAPI api)
    {
        auto nativeFS = std::make_shared<vfs::NativeFileSystem>();
        const std::filesystem::path startPath = app::GetDirectoryWithExecutable();
        const std::filesystem::path shaderDir = app::FindDirectoryWithShaderBin(
            api,
            *nativeFS,
            startPath,
            "shaders",
            "imgui_vertex");

        if (shaderDir.empty())
        {
            log::error(
                "Could not find compiled Donut shaders (imgui_vertex.bin). "
                "Rebuild the project so donut_shaders are available.");
        }

        return shaderDir;
    }

    std::filesystem::path FindScenesDirectory()
    {
        auto nativeFS = std::make_shared<vfs::NativeFileSystem>();
        const std::filesystem::path startPath = app::GetDirectoryWithExecutable();
        return app::FindDirectoryWithFile(*nativeFS, startPath, "scenes/manifest.json", 8);
    }
}

int main(int argc, char** argv)
{
    log::ConsoleApplicationMode();
    log::SetErrorMessageCaption("RenderLab");

    CommandLineOptions options;
    std::string parseError;
    if (!ParseCommandLine(argc, argv, options, parseError))
    {
        std::fprintf(stderr, "error: %s\n", parseError.c_str());
        PrintUsage("RenderLab");
        return 2;
    }

    if (options.help)
    {
        PrintUsage("RenderLab");
        return 0;
    }

    renderlab::GBufferDebugMode gbufferView = renderlab::GBufferDebugMode::BaseColor;
    if (options.gbufferView.has_value())
    {
        if (!renderlab::ParseGBufferDebugMode(*options.gbufferView, gbufferView, parseError))
        {
            std::fprintf(stderr, "error: %s\n", parseError.c_str());
            return 2;
        }
    }

    ValidationLog validationLog;
    validationLog.previous = log::GetCallback();
    log::SetCallback([&validationLog](log::Severity severity, char const* message) {
        if (severity >= log::Severity::Error)
        {
            ++validationLog.errorCount;
        }
        if (validationLog.previous)
        {
            validationLog.previous(severity, message);
        }
    });

    const nvrhi::GraphicsAPI api = nvrhi::GraphicsAPI::D3D12;
    std::unique_ptr<app::DeviceManager> deviceManager(app::DeviceManager::Create(api));
    if (!deviceManager)
    {
        log::error("Failed to create Donut DeviceManager.");
        return 1;
    }

    app::DeviceCreationParameters deviceParams;
    deviceParams.backBufferWidth = 1280;
    deviceParams.backBufferHeight = 720;
    deviceParams.swapChainFormat = nvrhi::Format::SRGBA8_UNORM;
    deviceParams.enableNvrhiValidationLayer = true;
#ifdef _DEBUG
    deviceParams.enableDebugRuntime = true;
#endif

    if (!deviceManager->CreateInstance(deviceParams))
    {
        log::error("Failed to create the DXGI instance.");
        return 1;
    }

    std::vector<app::AdapterInfo> adapters;
    if (!deviceManager->EnumerateAdapters(adapters) || adapters.empty())
    {
        log::error("Failed to enumerate DXGI adapters.");
        return 1;
    }

    for (int index = 0; index < static_cast<int>(adapters.size()); ++index)
    {
        const app::AdapterInfo& adapter = adapters[static_cast<size_t>(index)];
        log::info(
            "Adapter %d: %s (%u MB)",
            index,
            adapter.name.c_str(),
            static_cast<unsigned>(adapter.dedicatedVideoMemory / (1024 * 1024)));
    }

    const int adapterIndex = renderlab::SelectPreferredAdapterIndex(adapters);
    if (adapterIndex < 0)
    {
        log::error("No suitable D3D12 adapter was found.");
        return 1;
    }

    deviceParams.adapterIndex = adapterIndex;
    log::info(
        "Using adapter %d: %s",
        adapterIndex,
        adapters[static_cast<size_t>(adapterIndex)].name.c_str());

    if (!deviceManager->CreateWindowDeviceAndSwapChain(deviceParams, kWindowTitle))
    {
        log::error("Cannot initialize a graphics device with the requested parameters.");
        return 1;
    }

    const std::filesystem::path shaderDirectory =
        FindFrameworkShaderDirectory(deviceManager->GetGraphicsAPI());
    if (shaderDirectory.empty())
    {
        deviceManager->Shutdown();
        return 1;
    }

    const std::filesystem::path scenesDirectory = FindScenesDirectory();
    if (scenesDirectory.empty())
    {
        log::error(
            "Could not find scenes/manifest.json next to the executable or in the repository. "
            "Rebuild the project so scenes/ is copied next to RenderLab.exe.");
        deviceManager->Shutdown();
        return 1;
    }

    auto rootFS = std::make_shared<vfs::RootFileSystem>();
    rootFS->mount("/donut", shaderDirectory);
    rootFS->mount("/renderlab", shaderDirectory);
    rootFS->mount("/scenes", scenesDirectory);

    renderlab::SceneCatalog catalog;
    std::string catalogError;
    if (!renderlab::SceneCatalog::Load(*rootFS, "/scenes/manifest.json", catalog, catalogError))
    {
        log::error("%s", catalogError.c_str());
        deviceManager->Shutdown();
        return 1;
    }

    renderlab::ResolvedScene resolvedScene;
    if (!catalog.Resolve(options.scene, resolvedScene, catalogError))
    {
        log::error("%s", catalogError.c_str());
        deviceManager->Shutdown();
        return 2;
    }

    if (!renderlab::VerifySceneIntegrity(*rootFS, resolvedScene, catalogError))
    {
        log::error("%s", catalogError.c_str());
        deviceManager->Shutdown();
        return 1;
    }

    auto shaderFactory = std::make_shared<engine::ShaderFactory>(
        deviceManager->GetDevice(),
        rootFS,
        "/");

    const uint32_t defaultHeadlessFrames = 8;
    const uint32_t defaultDumpFrames = 4;
    const bool goldenOutput = options.output.has_value();
    const bool dumpViews = options.dumpGBufferViews.has_value() || goldenOutput;
    const bool limitedFrames = options.frames.has_value() || options.headless || dumpViews;
    const uint32_t dumpFrameIndex = 1;
    uint32_t frameLimit = options.frames.value_or(
        dumpViews && !options.headless ? defaultDumpFrames : defaultHeadlessFrames);
    if (dumpViews && frameLimit <= dumpFrameIndex)
    {
        frameLimit = dumpFrameIndex + 1;
    }

    renderlab::AppLaunchOptions launchOptions;
    launchOptions.scene = std::move(resolvedScene);
    launchOptions.camera = catalog.GetCameraPreset();
    launchOptions.lockCamera = options.lockCamera || options.headless || dumpViews;
    launchOptions.gbufferView = gbufferView;
    launchOptions.writeCaptureMetadata = goldenOutput;
    if (options.dumpGBufferViews.has_value())
    {
        launchOptions.dumpGBufferViewsDirectory = *options.dumpGBufferViews;
    }
    else if (goldenOutput)
    {
        launchOptions.dumpGBufferViewsDirectory = *options.output;
    }
    if (goldenOutput)
    {
        launchOptions.goldenOutputDirectory = *options.output;
    }

    const std::string goldenDirectory = launchOptions.goldenOutputDirectory;
    const bool writeMetadata = launchOptions.writeCaptureMetadata;

    int exitCode = 0;
    {
        renderlab::RenderingLabApp app(deviceManager.get(), shaderFactory, rootFS, std::move(launchOptions));
        if (!app.Init())
        {
            deviceManager->Shutdown();
            return 1;
        }

        renderlab::RenderingLabUserInterface ui(
            deviceManager.get(),
            app.GetCapabilities(),
            app.GetSceneHud(),
            app.GetGBufferTargets(),
            app.GetGBufferPassHud(),
            app.GetGBufferDebugHud());
        if (!ui.Init(shaderFactory))
        {
            log::error("Failed to initialize the ImGui renderer.");
            deviceManager->Shutdown();
            return 1;
        }

        std::unique_ptr<renderlab::markers::CpuMarker> frameCpuMarker;
        std::unique_ptr<renderlab::markers::CpuMarker> presentCpuMarker;
        nvrhi::CommandListHandle presentMarkerList = deviceManager->GetDevice()->createCommandList();

        const bool probeResize = !options.headless && !dumpViews && limitedFrames && frameLimit > 16;
        int minimizedPolls = 0;
        bool restoreAfterMinimize = false;
        deviceManager->m_callbacks.beforeFrame =
            [&frameCpuMarker, &restoreAfterMinimize, &minimizedPolls](app::DeviceManager& manager, uint32_t) {
                frameCpuMarker = std::make_unique<renderlab::markers::CpuMarker>(
                    manager.GetDevice(),
                    renderlab::markers::kFrame);
                if (restoreAfterMinimize)
                {
                    ++minimizedPolls;
                    if (minimizedPolls >= 4)
                    {
                        log::info("Smoke probe: restore window after minimize");
                        glfwRestoreWindow(manager.GetWindow());
                        restoreAfterMinimize = false;
                        minimizedPolls = 0;
                    }
                }
            };

        deviceManager->m_callbacks.beforePresent =
            [&presentCpuMarker, presentMarkerList](app::DeviceManager& manager, uint32_t) {
                presentCpuMarker = std::make_unique<renderlab::markers::CpuMarker>(
                    manager.GetDevice(),
                    renderlab::markers::kPresent);
                renderlab::markers::EmitStandaloneGpuMarker(
                    manager.GetDevice(),
                    presentMarkerList,
                    renderlab::markers::kPresent);
            };

        bool dumpFailed = false;
        deviceManager->m_callbacks.afterPresent =
            [&frameCpuMarker,
             &presentCpuMarker,
             limitedFrames,
             frameLimit,
             probeResize,
             &restoreAfterMinimize,
             &app,
             dumpViews,
             dumpFrameIndex,
             writeMetadata,
             &goldenDirectory,
             &dumpFailed](app::DeviceManager& manager, uint32_t frameIndex) {
                presentCpuMarker.reset();
                frameCpuMarker.reset();

                if (dumpViews && !app.GetGBufferDebugHud().dumpCompleted && frameIndex >= dumpFrameIndex)
                {
                    const std::string primaryDump = app.GetGBufferDebugHud().dumpDirectory;
                    if (!app.DumpGBufferDebugViews(primaryDump))
                    {
                        dumpFailed = true;
                        glfwSetWindowShouldClose(manager.GetWindow(), GLFW_TRUE);
                        return;
                    }
                    if (!goldenDirectory.empty() && goldenDirectory != primaryDump)
                    {
                        if (!app.DumpGBufferDebugViews(goldenDirectory))
                        {
                            dumpFailed = true;
                            glfwSetWindowShouldClose(manager.GetWindow(), GLFW_TRUE);
                            return;
                        }
                    }
                    if (writeMetadata)
                    {
                        const std::string metadataDir =
                            goldenDirectory.empty() ? primaryDump : goldenDirectory;
                        if (!app.WriteCaptureMetadata(metadataDir, frameIndex))
                        {
                            dumpFailed = true;
                            glfwSetWindowShouldClose(manager.GetWindow(), GLFW_TRUE);
                            return;
                        }
                    }
                }

                if (!limitedFrames)
                {
                    return;
                }

                if (probeResize && frameIndex == 8)
                {
                    int width = 0;
                    int height = 0;
                    manager.GetWindowDimensions(width, height);
                    glfwSetWindowSize(manager.GetWindow(), width + 64, height + 64);
                }

                if (probeResize && frameIndex == 12)
                {
                    log::info("Smoke probe: minimize window");
                    glfwIconifyWindow(manager.GetWindow());
                    restoreAfterMinimize = true;
                }

                if (frameIndex + 1 >= frameLimit)
                {
                    glfwSetWindowShouldClose(manager.GetWindow(), GLFW_TRUE);
                }
            };

        if (goldenOutput)
        {
            log::info(
                "Golden capture: scene=%s camera=%s resolution=1280x720 frame=%u output=%s",
                app.GetSceneHud().sceneId.c_str(),
                catalog.GetCameraPreset().name.c_str(),
                dumpFrameIndex,
                goldenDirectory.c_str());
        }

        if (options.headless)
        {
            glfwHideWindow(deviceManager->GetWindow());
            log::info(
                "Headless smoke: hidden window, locked camera, presenting %u frames.",
                frameLimit);
        }

        deviceManager->AddRenderPassToBack(&app);
        deviceManager->AddRenderPassToBack(&ui);
        deviceManager->RunMessageLoop();
        deviceManager->RemoveRenderPass(&ui);
        deviceManager->RemoveRenderPass(&app);

        if (limitedFrames)
        {
            log::info(
                "Smoke finished: frames=%u adapter=%s validation=%s errors=%d",
                deviceManager->GetFrameIndex(),
                app.GetCapabilities().adapterName.c_str(),
                app.GetCapabilities().validationMode.c_str(),
                validationLog.errorCount.load());
        }

        if (dumpViews && (dumpFailed || !app.GetGBufferDebugHud().dumpSucceeded))
        {
            log::error("GBuffer debug view dump did not complete successfully.");
            exitCode = 1;
        }

        if (validationLog.errorCount.load() > 0)
        {
            log::error(
                "Validation finished with %d NVRHI or D3D12 error(s).",
                validationLog.errorCount.load());
            exitCode = 1;
        }
    }

    deviceManager->Shutdown();
    return exitCode;
}
