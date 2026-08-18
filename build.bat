@echo off
setlocal EnableExtensions

cd /d "%~dp0"

if /I "%~1"=="help" goto :usage
if /I "%~1"=="--help" goto :usage
if /I "%~1"=="-h" goto :usage
if not "%~1"=="" goto :usage_error

where cmake.exe >nul 2>nul
if errorlevel 1 goto :cmake_missing

if not exist "%~dp0external\donut\CMakeLists.txt" goto :donut_missing

echo [RenderLab] Generating the Visual Studio 2022 x64 solution...
cmake.exe --fresh --preset windows-vs2022
if errorlevel 1 goto :generation_failed

set "RENDERLAB_SOLUTION=%~dp0out\build\windows-vs2022\RenderLab.sln"
if not exist "%RENDERLAB_SOLUTION%" goto :solution_missing

echo.
echo [RenderLab] Solution generated successfully:
echo %RENDERLAB_SOLUTION%
echo.
echo The application target will be added during implementation step S0.3.
exit /b 0

:usage
echo Usage: build.bat
echo.
echo Generates the Visual Studio 2022 x64 solution after Donut is bootstrapped.
exit /b 0

:usage_error
echo ERROR: build.bat does not accept build configurations.
echo Usage: build.bat
exit /b 2

:donut_missing
echo ERROR: Donut is missing at external\donut.
echo Run: powershell -NoProfile -File scripts\bootstrap.ps1
exit /b 1

:cmake_missing
echo ERROR: cmake.exe was not found. Install CMake 3.25 or newer, or add it to PATH.
exit /b 1

:generation_failed
echo ERROR: Visual Studio solution generation failed.
exit /b 1

:solution_missing
echo ERROR: CMake completed, but RenderLab.sln was not generated.
exit /b 1
