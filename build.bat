@echo off
setlocal enabledelayedexpansion

set BUILD_TYPE=%1
if "%BUILD_TYPE%"=="" set BUILD_TYPE=debug

if /I not "%BUILD_TYPE%"=="debug" if /I not "%BUILD_TYPE%"=="release" (
    echo ERROR: Unknown build type "%BUILD_TYPE%".
    echo Usage: build.bat [debug^|release]
    exit /b 1
)

set SCRIPT_DIR=%~dp0
pushd "%SCRIPT_DIR%"

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set VSINSTALL=%%i
    )
)

if not defined VSINSTALL (
    for %%p in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise"
    ) do (
        if exist "%%~p\VC\Auxiliary\Build\vcvarsall.bat" (
            set VSINSTALL=%%~p
            goto :found_vs
        )
    )
)

:found_vs
if not defined VSINSTALL (
    echo ERROR: Visual Studio Build Tools with the C++ workload were not found.
    echo Install Visual Studio Build Tools 2022 and include MSVC, Windows SDK, and resource compiler components.
    popd
    exit /b 1
)

call "%VSINSTALL%\VC\Auxiliary\Build\vcvarsall.bat" x64
if errorlevel 1 (
    echo ERROR: Failed to initialize the MSVC toolchain.
    popd
    exit /b 1
)

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: cl.exe is not available after MSVC environment initialization.
    popd
    exit /b 1
)

where rc.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: rc.exe is not available. Install the Windows SDK resource compiler component.
    popd
    exit /b 1
)

set OUT_DIR=%SCRIPT_DIR%build
if exist "%OUT_DIR%" rmdir /s /q "%OUT_DIR%"
mkdir "%OUT_DIR%"

if /I "%BUILD_TYPE%"=="release" (
    set CFLAGS=/nologo /std:c++20 /Zc:__cplusplus /permissive- /W4 /EHsc /utf-8 /O2 /MD /DNDEBUG /DUNICODE /D_UNICODE /DNOMINMAX /DSQLITE_ENABLE_FTS5
    set LDFLAGS=/nologo /INCREMENTAL:NO
) else (
    set CFLAGS=/nologo /std:c++20 /Zc:__cplusplus /permissive- /W4 /EHsc /utf-8 /Od /MDd /Zi /D_DEBUG /DUNICODE /D_UNICODE /DNOMINMAX /DSQLITE_ENABLE_FTS5
    set LDFLAGS=/nologo /DEBUG:FULL
)

set INCLUDES=/I"%SCRIPT_DIR%src" /I"%SCRIPT_DIR%third_party" /I"%SCRIPT_DIR%third_party\sqlite"
set SOURCES=src\main.cpp src\util.cpp src\storage.cpp src\openai_client.cpp src\prompt_dialog.cpp src\project_setup_dialog.cpp src\project_settings_dialog.cpp src\provider_manager.cpp src\mcp_manager.cpp src\mcp_server_manager.cpp src\rag_service.cpp src\rag_service_manager.cpp src\context_compression.cpp src\context_compression_manager.cpp third_party\sqlite\sqlite3.c
set LIBS=user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib shlwapi.lib ole32.lib winhttp.lib

rc /nologo /fo "%OUT_DIR%\app.res" app.rc
if errorlevel 1 (
    echo ERROR: Resource compilation failed.
    popd
    exit /b 1
)

cl %CFLAGS% %INCLUDES% /Fo"%OUT_DIR%\\" /Fd"%OUT_DIR%\\agent.pdb" /Fe"%OUT_DIR%\\agent.exe" %SOURCES% "%OUT_DIR%\app.res" /link %LDFLAGS% %LIBS%
if errorlevel 1 (
    echo ERROR: Build failed.
    popd
    exit /b 1
)

echo Build succeeded: "%OUT_DIR%\agent.exe"
popd
exit /b 0
