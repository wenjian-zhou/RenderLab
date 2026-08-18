#pragma once

#include <windows.h>

namespace renderlab::platform
{
class Win32Application final
{
  public:
    Win32Application(HINSTANCE instance, int showCommand, bool smokeTest) noexcept;
    ~Win32Application() noexcept;

    Win32Application(const Win32Application &) = delete;
    Win32Application &operator=(const Win32Application &) = delete;

    void Initialize();
    [[nodiscard]] HWND window() const noexcept;
    int Run();

  private:
    static constexpr wchar_t WindowClassName[] = L"RenderLabWindowClass";

    static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam,
                                            LPARAM lParam);

    LRESULT HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void RegisterWindowClass();
    void CreateMainWindow();
    void Cleanup() noexcept;

    HINSTANCE instance_ = nullptr;
    HWND window_ = nullptr;
    ATOM windowClass_ = 0;
    int showCommand_ = SW_SHOWNORMAL;
    bool smokeTest_ = false;
    bool windowLifecycleComplete_ = false;
    DWORD callbackError_ = ERROR_SUCCESS;
};
} // namespace renderlab::platform
