@echo off
setlocal

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set VSINSTALL=%%i
    )
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64

pushd %~dp0
cl /nologo /c /std:c++20 /Zc:__cplusplus /permissive- /W4 /EHsc /utf-8 /bigobj /Od /MDd /Zi /D_DEBUG /DUNICODE /D_UNICODE /DNOMINMAX /DSQLITE_ENABLE_FTS5 /Isrc /Ithird_party /Ithird_party\sqlite /Ithird_party\httplib /Ithird_party\openssl\include /Fo"build\temp_web_assets.obj" src\web_assets_default.cpp
popd
