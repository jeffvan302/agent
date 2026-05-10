@echo off
setlocal
setlocal EnableDelayedExpansion

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
set VSINSTALL=
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set VSINSTALL=%%i
    )
)

if not defined VSINSTALL (
    echo ERROR: Could not find VS install.
    exit /b 1
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 exit /b 1

cl /nologo /EHsc /Od /Zi "%~dp0test_ollama_pipe.cpp" /Fe:"%~dp0test_ollama_pipe.exe"
if errorlevel 1 exit /b 1

echo Compiled. Running...
"%~dp0test_ollama_pipe.exe"
