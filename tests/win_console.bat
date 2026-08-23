@echo off
setlocal

REM Build and run the Windows console tests.  They need a real console: the
REM extended key path in os\windows\platform.cc sits behind is_terminal(), so
REM nothing reached through a pipe exercises it.  Uses the same Visual Studio
REM as src\do_build.bat.

set VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community
set VCVARS="%VSDIR%\VC\Auxiliary\Build\vcvarsall.bat"

if not exist %VCVARS% (
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

if "%~1"=="" (
    "%~dp0win_console.exe" ..\src\cpmemu.exe
) else (
    "%~dp0win_console.exe" %*
)
exit /b %errorlevel%
