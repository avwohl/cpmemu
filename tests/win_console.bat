@echo off
setlocal enabledelayedexpansion

REM Build and run the Windows console tests.  They need a real console: the
REM extended key path in os\windows\platform.cc sits behind is_terminal(), so
REM nothing reached through a pipe exercises it.
REM
REM Finding the compiler used to be one absolute path - "...\Visual Studio\18\
REM Community" - and a SKIP when it was not there.  That is why these tests had
REM never run anywhere: the only machine that could finally run them is a
REM GitHub Actions windows runner, whose install is "...\Visual Studio\2022\
REM Enterprise", so the runner took the SKIP, and a SKIP exits 0, so CI went
REM green having executed nothing.  vswhere.exe ships with every Visual Studio
REM installer since 2017 and reports what is actually installed; the hardcoded
REM path stays as the last resort for a machine whose installer is gone.
REM
REM CPMEMU_REQUIRE_MSVC=1 turns every SKIP in here into a FAIL.  CI sets it,
REM because in CI a skip is the one outcome that must not be read as a pass.

set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSDIR=%%i"
)
if not defined VSDIR set "VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community"

set VCVARS="%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat"

if not exist %VCVARS% (
    if "%CPMEMU_REQUIRE_MSVC%"=="1" (
        echo FAIL  windows console tests ^(no vcvarsall.bat at %VCVARS%^)
        exit /b 1
    )
    echo SKIP  windows console tests ^(no vcvarsall.bat at %VCVARS%^)
    exit /b 0
)

call %VCVARS% x64 >nul 2>&1
cd /d "%~dp0"

cl /nologo /EHsc /O2 /std:c++14 win_console.cc /Fe:win_console.exe /Fo:win_console.obj user32.lib >nul
if errorlevel 1 (
    echo FAIL  windows console tests ^(win_console.cc did not compile^)
    cl /nologo /EHsc /O2 /std:c++14 win_console.cc /Fe:win_console.exe /Fo:win_console.obj user32.lib
    exit /b 1
)

REM --require passes the same rule down: win_console.exe skips when it can find
REM no emulator and no console, and those skips also exit 0 on their own.
set "REQUIRE="
if "%CPMEMU_REQUIRE_MSVC%"=="1" set "REQUIRE=--require"

if "%~1"=="" (
    "%~dp0win_console.exe" %REQUIRE% ..\src\cpmemu.exe
) else (
    "%~dp0win_console.exe" %REQUIRE% %*
)
exit /b %errorlevel%
