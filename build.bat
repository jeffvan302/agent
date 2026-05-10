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

where cl.exe >/dev/null 2>/dev/null
if errorlevel 1 (
    echo ERROR: cl.exe is not available after MSVC environment initialization.
    popd
    exit /b 1
)

where rc.exe >/dev/null 2>/dev/null
if errorlevel 1 (
    echo ERROR: rc.exe is not available. Install the Windows SDK resource compiler component.
    popd
    exit /b 1
)

if not exist "%SCRIPT_DIR%.config.zip" (
    echo ERROR: .config.zip was not found. Setup System embeds this file into agent.exe at build time.
    popd
    exit /b 1
)

set OUT_DIR=%SCRIPT_DIR%build
if exist "%OUT_DIR%" rmdir /s /q "%OUT_DIR%"
mkdir "%OUT_DIR%"

if /I "%BUILD_TYPE%"=="release" (
    set CFLAGS=/nologo /std:c++20 /Zc:__cplusplus /permissive- /W4 /EHsc /utf-8 /bigobj /O2 /MD /DNDEBUG /DUNICODE /D_UNICODE /DNOMINMAX /DSQLITE_ENABLE_FTS5
    set LDFLAGS=/nologo /INCREMENTAL:NO
) else (
    set CFLAGS=/nologo /std:c++20 /Zc:__cplusplus /permissive- /W4 /EHsc /utf-8 /bigobj /Od /MDd /Z7 /D_DEBUG /DUNICODE /D_UNICODE /DNOMINMAX /DSQLITE_ENABLE_FTS5
    set LDFLAGS=/nologo /DEBUG:FULL
)

set INCLUDES=/I"%SCRIPT_DIR%src" /I"%SCRIPT_DIR%third_party" /I"%SCRIPT_DIR%third_party\sqlite" /I"%SCRIPT_DIR%third_party\httplib"
set SOURCES=src\main.cpp src\util.cpp src\storage.cpp src\chat_request_logger.cpp src\openai_client.cpp src\ollama_cli_bridge.cpp src\ollama_local_server.cpp src\ollama_api_client.cpp src\prompt_dialog.cpp src\questionnaire_dialog.cpp src\project_setup_dialog.cpp src\project_settings_dialog.cpp src\queue_test_dialog.cpp src\provider_manager.cpp src\provider_catalog.cpp src\provider_auth_bridge.cpp src\mcp_manager.cpp src\mcp_server_manager.cpp src\rag_service.cpp src\rag_service_manager.cpp src\remote_ollama_worker.cpp src\remote_provider_worker.cpp src\remote_worker_setup_dialog.cpp src\context_compression.cpp src\context_compression_manager.cpp src\model_tools_manager.cpp src\agentic_modes_manager.cpp src\web_user_store.cpp src\web_server.cpp src\web_assets_default.cpp src\web_config_dialog.cpp src\admin_config_dialog.cpp src\openssl_applink.c third_party\sqlite\sqlite3.c
set LIBS=user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib shlwapi.lib ole32.lib winhttp.lib cabinet.lib bcrypt.lib crypt32.lib advapi32.lib

rem -- OpenSSL auto-detection ---------------------------------------------------
rem If third_party\openssl\ exists and contains the expected headers, enable HTTPS.
rem Run  scripts\download_openssl.ps1  to populate that directory automatically.
rem
rem Static linking is strongly preferred -- it embeds OpenSSL into agent.exe so
rem no separate DLLs are needed on end-user machines.
rem
rem Static lib search order:
rem   1. third_party\openssl\lib\VC\x64\MD\libssl_static.lib   (old naming, release)
rem   2. third_party\openssl\lib\VC\x64\MD\libssl.lib           (new naming 3.4+, release)
rem   3. same two checks for MDd (debug)
rem   4. third_party\openssl\lib\libssl_static.lib              (flat layout)
rem   5. third_party\openssl\lib\libssl.lib                     (import lib -- dynamic)
rem
set OPENSSL_DIR=%SCRIPT_DIR%third_party\openssl
set OPENSSL_SSL_LIB=
set OPENSSL_CRYPTO_LIB=
set OPENSSL_STATIC=0

if not exist "%OPENSSL_DIR%\include\openssl\ssl.h" goto :no_openssl

rem -- Pick VC subdirectory for this runtime variant ---------------------------
if /I "%BUILD_TYPE%"=="release" (
    set OPENSSL_STATIC_DIR=%OPENSSL_DIR%\lib\VC\x64\MD
) else (
    set OPENSSL_STATIC_DIR=%OPENSSL_DIR%\lib\VC\x64\MDd
)

rem -- Try _static suffix first (OpenSSL 3.3 and earlier) ----------------------
if exist "%OPENSSL_STATIC_DIR%\libssl_static.lib" (
    set OPENSSL_SSL_LIB=%OPENSSL_STATIC_DIR%\libssl_static.lib
    set OPENSSL_CRYPTO_LIB=%OPENSSL_STATIC_DIR%\libcrypto_static.lib
    set OPENSSL_STATIC=1
    goto :openssl_found
)

rem -- Try plain name in VC subdir (OpenSSL 3.4+ renamed _static -> plain) -----
rem    libs in lib\VC\x64\MD[d]\ are ALWAYS the static variants
if exist "%OPENSSL_STATIC_DIR%\libssl.lib" (
    set OPENSSL_SSL_LIB=%OPENSSL_STATIC_DIR%\libssl.lib
    set OPENSSL_CRYPTO_LIB=%OPENSSL_STATIC_DIR%\libcrypto.lib
    set OPENSSL_STATIC=1
    goto :openssl_found
)

rem -- Flat lib\ fallbacks (static suffix) -------------------------------------
if exist "%OPENSSL_DIR%\lib\libssl_static.lib" (
    set OPENSSL_SSL_LIB=%OPENSSL_DIR%\lib\libssl_static.lib
    set OPENSSL_CRYPTO_LIB=%OPENSSL_DIR%\lib\libcrypto_static.lib
    set OPENSSL_STATIC=1
    goto :openssl_found
)

rem -- Last resort: flat lib\libssl.lib = import lib (dynamic DLL required) ----
if exist "%OPENSSL_DIR%\lib\libssl.lib" (
    set OPENSSL_SSL_LIB=%OPENSSL_DIR%\lib\libssl.lib
    set OPENSSL_CRYPTO_LIB=%OPENSSL_DIR%\lib\libcrypto.lib
    set OPENSSL_STATIC=0
    goto :openssl_found
)

goto :no_openssl

:openssl_found
echo OpenSSL found -- enabling HTTPS/TLS support.
if "%OPENSSL_STATIC%"=="1" (
    echo   Linking STATICALLY -- no runtime DLLs needed.
) else (
    echo   Linking dynamically -- DLLs will be copied to build directory.
)
set CFLAGS=%CFLAGS% /DCPPHTTPLIB_OPENSSL_SUPPORT
set INCLUDES=%INCLUDES% /I"%OPENSSL_DIR%\include"
rem applink.c is compiled via src\openssl_applink.c (already in SOURCES above).
rem OpenSSL static builds also need these Windows system libs:
set LIBS=%LIBS% %OPENSSL_SSL_LIB% %OPENSSL_CRYPTO_LIB% Crypt32.lib ws2_32.lib

if "%OPENSSL_STATIC%"=="0" (
    rem Copy runtime DLLs to build output so the exe runs in-place
    for %%f in ("%OPENSSL_DIR%\bin\*.dll") do (
        copy /y "%%f" "%OUT_DIR%\" >/dev/null 2>&1
    )
)

:no_openssl

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
