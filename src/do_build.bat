@echo off
setlocal

REM Use the same VS path as z80cpmw
set VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community
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
