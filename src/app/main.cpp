#include "RenderingLabApp.h"

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
        bool help = false;
    };

    struct ValidationLog
    {
        std::atomic<int> errorCount{0};
        log::Callback previous;
    };

    void PrintUsage(const char* executable)
    {
        std::printf(
            "Usage: %s [options]\n"
            "\n"
            "Options:\n"
            "  --help              Show this help message\n"
            "  --scene <path>      Scene path (not implemented; scheduled for S0.4)\n"
            "  --headless          Headless device (not implemented; scheduled for S0.5)\n"
            "  --frames <n>        Present n frames, then exit\n"
            "  --output <path>     Capture output path (not implemented; scheduled for S0.5)\n"
            "  --dx12, --d3d12     Select the D3D12 backend (default and only supported API)\n"
            "\n"
            "RenderLab is D3D12-only. Donut DeviceManager owns the window, device, queues,\n"
            "fences, and swap chain.\n",
            executable);
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

    bool RejectUnimplementedOptions(const CommandLineOptions& options, std::string& error)
    {
        if (options.scene.has_value())
        {
            error = "--scene is not implemented (scheduled for S0.4).";
            return false;
        }
        if (options.headless)
        {
            error = "--headless is not implemented (scheduled for S0.5).";
            return false;
        }
        if (options.output.has_value())
        {
            error = "--output is not implemented (scheduled for S0.5).";
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
                "Could not find compiled Donut shaders (imgui_vertex.bin) above %s.",
                startPath.generic_string().c_str());
        }

        return shaderDir;
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
        PrintUsage(argv[0] ? argv[0] : "RenderLab");
        return 2;
    }

    if (options.help)
    {
        PrintUsage(argv[0] ? argv[0] : "RenderLab");
        return 0;
    }

    if (!RejectUnimplementedOptions(options, parseError))
    {
        std::fprintf(stderr, "error: %s\n", parseError.c_str());
        return 2;
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

    auto rootFS = std::make_shared<vfs::RootFileSystem>();
    rootFS->mount("/donut", shaderDirectory);
    auto shaderFactory = std::make_shared<engine::ShaderFactory>(
        deviceManager->GetDevice(),
        rootFS,
        "/");

    int exitCode = 0;
    {
        renderlab::RenderingLabApp app(deviceManager.get());
        if (!app.Init())
        {
            deviceManager->Shutdown();
            return 1;
        }

        renderlab::RenderingLabUserInterface ui(deviceManager.get(), app.GetCapabilities());
        if (!ui.Init(shaderFactory))
        {
            log::error("Failed to initialize the ImGui renderer.");
            deviceManager->Shutdown();
            return 1;
        }

        if (options.frames.has_value())
        {
            const uint32_t frameLimit = *options.frames;
            deviceManager->m_callbacks.afterPresent =
                [frameLimit](app::DeviceManager& manager, uint32_t frameIndex) {
                    if (frameIndex == 8 && frameLimit > 16)
                    {
                        int width = 0;
                        int height = 0;
                        manager.GetWindowDimensions(width, height);
                        glfwSetWindowSize(manager.GetWindow(), width + 64, height + 64);
                    }

                    if (frameIndex + 1 >= frameLimit)
                    {
                        glfwSetWindowShouldClose(manager.GetWindow(), GLFW_TRUE);
                    }
                };
        }

        deviceManager->AddRenderPassToBack(&app);
        deviceManager->AddRenderPassToBack(&ui);
        deviceManager->RunMessageLoop();
        deviceManager->RemoveRenderPass(&ui);
        deviceManager->RemoveRenderPass(&app);

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
