#include "platform/win32/win32_application.h"

#include <shellapi.h>
#include <windows.h>

#include <exception>
#include <memory>
#include <string_view>
#include <system_error>

namespace
{
struct LocalFreeDeleter
{
    void operator()(wchar_t **memory) const noexcept
    {
        LocalFree(memory);
    }
};

bool HasSmokeTestArgument()
{
    int argumentCount = 0;
    std::unique_ptr<wchar_t *, LocalFreeDeleter> arguments(
        CommandLineToArgvW(GetCommandLineW(), &argumentCount));
    if (!arguments)
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "CommandLineToArgvW");
    }

    bool smokeTest = false;
    for (int index = 1; index < argumentCount; ++index)
    {
        const std::wstring_view argument(arguments.get()[index]);
        if (argument == L"--smoke-test" || argument == L"--smoke-test=window-lifecycle")
        {
            smokeTest = true;
        }
    }

    return smokeTest;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand)
{
    bool smokeTest =
        std::wstring_view(GetCommandLineW()).find(L"--smoke-test") != std::wstring_view::npos;

    try
    {
        smokeTest = HasSmokeTestArgument();
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
