#include "platform/win32/win32_application.h"

#include <stdexcept>
#include <string>
#include <system_error>

namespace renderlab::platform
{
namespace
{
[[noreturn]] void ThrowLastError(const char *operation)
{
    const DWORD error = GetLastError();
    throw std::system_error(static_cast<int>(error), std::system_category(), operation);
}
} // namespace

Win32Application::Win32Application(HINSTANCE instance, int showCommand, bool smokeTest) noexcept
    : instance_(instance), showCommand_(showCommand), smokeTest_(smokeTest)
{
}

int Win32Application::Run()
{
    try
    {
        RegisterWindowClass();
        CreateMainWindow();

        ShowWindow(window_, smokeTest_ ? SW_HIDE : showCommand_);
        UpdateWindow(window_);

        if (smokeTest_ && !PostMessageW(window_, WM_CLOSE, 0, 0))
        {
            ThrowLastError("PostMessageW");
        }

        MSG message = {};
        while (true)
        {
            const BOOL result = GetMessageW(&message, nullptr, 0, 0);
            if (result == -1)
            {
                ThrowLastError("GetMessageW");
            }

            if (result == 0)
            {
                Cleanup();

                if (callbackError_ != ERROR_SUCCESS)
                {
                    throw std::system_error(static_cast<int>(callbackError_),
                                            std::system_category(), "Win32 window callback");
                }

                if (smokeTest_ && !windowLifecycleComplete_)
                {
                    throw std::runtime_error("Win32 window teardown did not complete");
                }

                return static_cast<int>(message.wParam);
            }

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    catch (...)
    {
        Cleanup();
        throw;
    }
}

void Win32Application::RegisterWindowClass()
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProcedure;
    windowClass.hInstance = instance_;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = WindowClassName;

    windowClass_ = RegisterClassExW(&windowClass);
    if (windowClass_ == 0)
    {
        ThrowLastError("RegisterClassExW");
    }
}

void Win32Application::CreateMainWindow()
{
    constexpr LONG ClientWidth = 1280;
    constexpr LONG ClientHeight = 720;
    constexpr DWORD WindowStyle = WS_OVERLAPPEDWINDOW;

    RECT windowRectangle = {0, 0, ClientWidth, ClientHeight};
    if (!AdjustWindowRectEx(&windowRectangle, WindowStyle, FALSE, 0))
    {
        ThrowLastError("AdjustWindowRectEx");
    }

    window_ = CreateWindowExW(0, WindowClassName, L"RenderLab", WindowStyle, CW_USEDEFAULT,
                              CW_USEDEFAULT, windowRectangle.right - windowRectangle.left,
                              windowRectangle.bottom - windowRectangle.top, nullptr, nullptr,
                              instance_, this);

    if (window_ == nullptr)
    {
        if (callbackError_ != ERROR_SUCCESS)
        {
            throw std::system_error(static_cast<int>(callbackError_), std::system_category(),
                                    "SetWindowLongPtrW");
        }

        ThrowLastError("CreateWindowExW");
    }
}

void Win32Application::Cleanup() noexcept
{
    if (window_ != nullptr && IsWindow(window_))
    {
        DestroyWindow(window_);
    }
    window_ = nullptr;

    if (windowClass_ != 0)
    {
        UnregisterClassW(WindowClassName, instance_);
        windowClass_ = 0;
    }
}

LRESULT CALLBACK Win32Application::WindowProcedure(HWND window, UINT message, WPARAM wParam,
                                                   LPARAM lParam)
{
    Win32Application *application =
        reinterpret_cast<Win32Application *>(GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
        application = static_cast<Win32Application *>(create->lpCreateParams);

        if (application == nullptr)
        {
            return FALSE;
        }

        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previousValue =
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(application));
        const DWORD callbackError = GetLastError();
        if (previousValue == 0 && callbackError != ERROR_SUCCESS)
        {
            application->callbackError_ = callbackError;
            return FALSE;
        }

        application->window_ = window;
    }

    if (application != nullptr)
    {
        return application->HandleMessage(window, message, wParam, lParam);
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

LRESULT Win32Application::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_PAINT: {
        PAINTSTRUCT paint = {};
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY: {
        const LRESULT result = DefWindowProcW(window, message, wParam, lParam);

        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previousValue = SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        const DWORD callbackError = GetLastError();
        if (previousValue == 0 && callbackError != ERROR_SUCCESS)
        {
            callbackError_ = callbackError;
        }

        if (window_ == window)
        {
            window_ = nullptr;
        }
        windowLifecycleComplete_ = true;
        return result;
    }

    default:
        return DefWindowProcW(window, message, wParam, lParam);
    }
}
} // namespace renderlab::platform
