@echo off
setlocal enabledelayedexpansion

REM Ask the installer where Visual Studio is rather than naming one path.
REM This used to hardcode "...\Visual Studio\18\Community" - the same path
REM z80cpmw uses - and fail outright anywhere else, which is every CI runner
REM (a GitHub Actions windows image installs 2022 Enterprise) and every machine
REM with a different edition.  vswhere.exe ships with every Visual Studio
REM installer since 2017; the old path stays as the last resort.
set "VSDIR="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`) do set "VSDIR=%%i"
)
if not defined VSDIR set "VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community"

set VCVARS="%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat"

if not exist %VCVARS% (
    echo ERROR: Cannot find vcvarsall.bat at %VCVARS%
    exit /b 1
)

call %VCVARS% x64 >nul 2>&1

cd /d "%~dp0"

echo.
echo Building cpmemu for Windows x64...
echo.

echo [1/6] Compiling qkz80.cc...
cl /nologo /c /EHsc /O2 /std:c++14 /I. qkz80.cc
if errorlevel 1 goto :error

echo [2/6] Compiling qkz80_errors.cc...
cl /nologo /c /EHsc /O2 /std:c++14 /I. qkz80_errors.cc
if errorlevel 1 goto :error

echo [3/6] Compiling qkz80_mem.cc...
cl /nologo /c /EHsc /O2 /std:c++14 /I. qkz80_mem.cc
if errorlevel 1 goto :error

echo [4/6] Compiling qkz80_reg_set.cc...
cl /nologo /c /EHsc /O2 /std:c++14 /I. qkz80_reg_set.cc
if errorlevel 1 goto :error

echo [5/6] Compiling os\windows\platform.cc...
cl /nologo /c /EHsc /O2 /std:c++14 /I. os\windows\platform.cc /Foplatform.obj
if errorlevel 1 goto :error

echo [6/6] Compiling cpmemu.cc...
cl /nologo /c /EHsc /O2 /std:c++14 /I. cpmemu.cc
if errorlevel 1 goto :error

echo.
echo Linking cpmemu.exe...
link /nologo /OUT:cpmemu.exe cpmemu.obj qkz80.obj qkz80_errors.obj qkz80_mem.obj qkz80_reg_set.obj platform.obj
if errorlevel 1 goto :error

echo.
echo ========================================
echo BUILD SUCCESSFUL
echo ========================================
dir /b cpmemu.exe
goto :end

:error
echo.
echo ========================================
echo BUILD FAILED
echo ========================================
exit /b 1

:end
endlocal
