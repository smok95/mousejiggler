@echo off
REM Simple build script for Mouse Jiggler C++ version
REM This assumes you have Visual Studio installed and want to build from command line

echo Building Mouse Jiggler C++ Win32 Version...
echo.

REM Check if msbuild is available
where msbuild >nul 2>nul
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: MSBuild not found in PATH
    echo Please run this from a Visual Studio Developer Command Prompt
    echo or open MouseJiggler.vcxproj in Visual Studio
    pause
    exit /b 1
)

REM Default to Release x64 build
set CONFIG=Release
set PLATFORM=x64

if "%1" NEQ "" set CONFIG=%1
if "%2" NEQ "" set PLATFORM=%2

echo Configuration: %CONFIG%
echo Platform: %PLATFORM%
echo.

msbuild MouseJiggler.vcxproj /p:Configuration=%CONFIG% /p:Platform=%PLATFORM% /v:minimal

if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful!
    echo Output: bin\%PLATFORM%\%CONFIG%\MouseJiggler.exe
) else (
    echo.
    echo Build failed!
)

echo.
pause
