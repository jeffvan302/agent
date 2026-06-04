# C++ Development with Microsoft Compiler — Clean System Setup Guide

> **Goal:** Starting from a fresh Windows machine with nothing installed, set up a complete C++ development environment using the Microsoft MSVC compiler, VS Code as the editor, and PowerShell as the build terminal. No Visual Studio Professional needed — everything is free.

---

## Agent-Friendly PowerShell Install

When a model or automation needs to prepare a Windows machine for MSVC C++ development, run this from an **Administrator PowerShell** window. It installs the C++ Build Tools workload, CMake, Git, VS Code, and a reusable `Set-VsDevEnv` helper for normal PowerShell sessions.

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

# 1. Install Visual Studio Build Tools with the C++ workload.
winget install --id Microsoft.VisualStudio.2022.BuildTools --exact `
  --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" `
  --accept-source-agreements `
  --accept-package-agreements

# 2. Install command-line tools and editor.
winget install --id Kitware.CMake --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
winget install --id Git.Git --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
winget install --id Microsoft.VisualStudioCode --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements

# 3. Install VS Code extensions.
code --install-extension ms-vscode.cpptools
code --install-extension ms-vscode.cmake-tools

# 4. Add a PowerShell helper that loads the MSVC developer environment.
$profileDir = Split-Path $PROFILE
if (-not (Test-Path $profileDir)) { New-Item -ItemType Directory -Path $profileDir -Force | Out-Null }

$vsEnvFunction = @'
function Set-VsDevEnv {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vsWhere)) {
        $vsWhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    }

    $vsPath = & $vsWhere -latest -property installationPath 2>$null
    if (-not $vsPath) {
        Write-Host "Visual Studio or Build Tools not found." -ForegroundColor Red
        return
    }

    $devShell = Join-Path $vsPath "Common7\Tools\Launch-VsDevShell.ps1"
    if (Test-Path $devShell) {
        & $devShell -Arch amd64
        return
    }

    $devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
    if (Test-Path $devCmd) {
        cmd /s /c "`"$devCmd`" -arch=amd64 && set" | ForEach-Object {
            if ($_ -match "^([^=]+)=(.*)$") {
                [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
            }
        }
    }
}
'@

if (Test-Path $PROFILE) {
    $profileContent = Get-Content $PROFILE -Raw
    if ($profileContent -notmatch "function Set-VsDevEnv") {
        Add-Content -Path $PROFILE -Value "`n$vsEnvFunction"
    }
} else {
    Set-Content -Path $PROFILE -Value $vsEnvFunction
}

# 5. Load and verify in this session.
. $PROFILE
Set-VsDevEnv
cl
cmake --version
git --version
```

Close and reopen PowerShell after the script finishes, then run `Set-VsDevEnv` before C++ builds that call `cl.exe` directly.

---

## The Key Insight: You Have Two Free Options

There are **two ways** to get the MSVC compiler without paying for Visual Studio Professional:

| Option | What You Get | Size | License |
|---|---|---|---|
| **A. Visual Studio Build Tools** | Just the compiler, libraries, and Windows SDK — no IDE | ~6 GB | Free for open-source; requires a VS license for commercial use (see note) |
| **B. Visual Studio Community** | Full IDE + compiler + everything | ~10-20 GB | **Free for individual developers, students, and open-source contributors** |

### Which Should You Choose?

**If you only want to compile from PowerShell and VS Code** → **Option A (Build Tools)** is the leanest choice. It installs only the compiler toolchain without the Visual Studio IDE.

**If you might ever want the full IDE** → **Option B (Community)** gives you everything, including the compiler, and it's free for individual developers. You can still compile from PowerShell — the IDE is just there if you need it.

> **License note for Build Tools:** Microsoft's license says Build Tools require a valid Visual Studio license "unless you are building open-source dependencies for your project." If you're building closed-source/commercial software and don't own a VS license, you should use **Visual Studio Community** instead, which is free for individual developers.

---

## Step 1 — Install the MSVC Compiler Toolchain

### Option A: Build Tools for Visual Studio 2022 (Lean — No IDE)

**Download page:** [https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022)

1. Go to the page above.
2. Scroll down to **"Tools for Visual Studio"**.
3. Click **Download** next to **"Build Tools for Visual Studio 2022"**.
4. Run the downloaded `vs_BuildTools.exe`.
5. The Visual Studio Installer opens. Check this workload:
   - ✅ **Desktop development with C++**
6. On the right side, make sure these individual components are checked (they should be auto-selected):
   - ✅ MSVC v143 - VS 2022 C++ x64/x86 build tools
   - ✅ Windows 11 SDK (or Windows 10 SDK)
   - ✅ C++ CMake tools for Windows
7. Click **Install**. This downloads ~6 GB.

### Option B: Visual Studio Community 2022 (Full IDE — Free for Individuals)

**Download page:** [https://visualstudio.microsoft.com/vs/community/](https://visualstudio.microsoft.com/vs/community/)

1. Go to the page above.
2. Click **Free download**.
3. Run the installer.
4. Check this workload:
   - ✅ **Desktop development with C++**
5. Click **Install**. This downloads ~10-20 GB depending on what's selected.

> **Community is free for:** individual developers, students, open-source contributors, and non-enterprise organizations (up to 5 users). If you're an individual building your own apps, you're covered.

### Install via winget (Either Option)

You can also install from PowerShell:

```powershell
# Option A: Build Tools only (no IDE)
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"

# Option B: Visual Studio Community (full IDE)
winget install Microsoft.VisualStudio.2022.Community --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"
```

> **Note:** The `--override` flag passes arguments directly to the Visual Studio Installer. `--passive` means it installs without showing UI prompts. The install will take 10-30 minutes depending on your internet speed.

---

## Step 2 — Install CMake (Build System)

CMake is the standard build system for C++ projects. It generates the files that tell the compiler what to build.

**Download page:** [https://cmake.org/download/](https://cmake.org/download/)

### Install via winget:

```powershell
winget install Kitware.CMake
```

### Or download manually:

1. Go to [https://cmake.org/download/](https://cmake.org/download/)
2. Download **cmake-x.x.x-windows-x86_64.msi** (the Windows installer)
3. Run it — accept all defaults
4. **Important:** During install, check **"Add CMake to the system PATH for all users"**

### Verify:

```powershell
cmake --version
```

You should see something like `cmake version 3.31.x`.

---

## Step 3 — Install Git

**Download page:** [https://git-scm.com/download/win](https://git-scm.com/download/win)

### Install via winget:

```powershell
winget install Git.Git
```

### Verify:

```powershell
git --version
```

---

## Step 4 — Install VS Code

**Download page:** [https://code.visualstudio.com](https://code.visualstudio.com)

### Install via winget:

```powershell
winget install Microsoft.VisualStudioCode
```

### Install the C++ Extension

Open VS Code and press `Ctrl+Shift+X` to open Extensions, then search for and install:

| Extension | What It Does | Install Command |
|---|---|---|
| **C/C++** (by Microsoft) | IntelliSense, debugging, code navigation | `code --install-extension ms-vscode.cpptools` |
| **CMake Tools** (by Microsoft) | CMake project configuration and build | `code --install-extension ms-vscode.cmake-tools` |

Or install from PowerShell:

```powershell
code --install-extension ms-vscode.cpptools
code --install-extension ms-vscode.cmake-tools
```

---

## Step 5 — Understand the Developer Command Prompt

This is the most important thing to understand about MSVC development from the command line:

**The MSVC compiler (`cl.exe`) does NOT work in a regular PowerShell window.** It needs special environment variables that are set by the "Developer Command Prompt."

There are three ways to handle this:

### Method 1: Use the Developer Command Prompt (Simplest)

1. Press the Windows key.
2. Type **"Developer Command Prompt"** or **"Developer PowerShell for VS"**.
3. Open it.
4. Navigate to your project and run `cl` or `cmake` from there.

This is the easiest approach. The shortcut automatically sets all the environment variables that `cl.exe` needs.

### Method 2: Launch VS Code from the Developer Command Prompt

```powershell
# Open a Developer Command Prompt for VS
# Then navigate to your project and launch VS Code:
cd C:\path\to\your\project
code .
```

VS Code inherits the environment variables from the terminal that launched it, so `cl.exe` will work inside VS Code's integrated terminal.

### Method 3: Source the environment in regular PowerShell (Recommended for Automation)

Add this to your PowerShell profile so every PowerShell session can use `cl.exe`:

```powershell
notepad $PROFILE
```

Add this function:

```powershell
function Set-VsDevEnv {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vsWhere)) {
        $vsWhere = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    }
    
    $vsPath = & $vsWhere -latest -property installationPath 2>$null
    
    if ($vsPath) {
        $devCmd = Join-Path $vsPath "Common7\Tools\Launch-VsDevShell.ps1"
        if (Test-Path $devCmd) {
            & $devCmd -Arch amd64
            Write-Host "MSVC developer environment loaded." -ForegroundColor Green
        } else {
            # Fallback for Build Tools
            $devCmd2 = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
            if (Test-Path $devCmd2) {
                cmd /s /c "`"$devCmd2`" -arch=amd64 && set" | ForEach-Object {
                    if ($_ -match "^([^=]+)=(.*)$") {
                        [System.Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
                    }
                }
                Write-Host "MSVC developer environment loaded (fallback)." -ForegroundColor Green
            }
        }
    } else {
        Write-Host "Visual Studio or Build Tools not found." -ForegroundColor Red
    }
}

# Auto-load on startup (optional — uncomment to enable)
# Set-VsDevEnv
```

Then in any PowerShell session, you can run:

```powershell
Set-VsDevEnv
cl      # Should print Microsoft C/C++ Compiler version
cmake   # Should print CMake version
```

---

## Step 6 — Verify the Compiler Works

After installing everything, test that the MSVC compiler is accessible:

### Test 1: Check cl.exe

Open a **Developer Command Prompt for VS** (or run `Set-VsDevEnv` in PowerShell) and type:

```powershell
cl
```

You should see output like:
```
Microsoft (R) C/C++ Optimizing Compiler Version 19.xx.xxxxx for x64
```

### Test 2: Compile a Hello World Program

Create a test file:

```powershell
mkdir C:\Dev\CppTest
cd C:\Dev\CppTest
@'
#include <iostream>

int main() {
    std::cout << "Hello from MSVC!" << std::endl;
    return 0;
}
'@ | Out-File -Encoding ascii -FilePath "hello.cpp"
```

Compile it:

```powershell
cl /EHsc hello.cpp
```

Run it:

```powershell
.\hello.exe
```

You should see: `Hello from MSVC!`

### Test 3: Build with CMake

```powershell
mkdir C:\Dev\CMakeTest
cd C:\Dev\CMakeTest
```

Create `main.cpp`:

```powershell
@'
#include <iostream>

int main() {
    std::cout << "Hello from CMake + MSVC!" << std::endl;
    return 0;
}
'@ | Out-File -Encoding ascii -FilePath "main.cpp"
```

Create `CMakeLists.txt`:

```powershell
@'
cmake_minimum_required(VERSION 3.20)
project(HelloCMake)

set(CMAKE_CXX_STANDARD 17)

add_executable(HelloCMake main.cpp)
'@ | Out-File -Encoding ascii -FilePath "CMakeLists.txt"
```

Build it:

```powershell
# Make sure MSVC environment is loaded
Set-VsDevEnv

# Configure
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release

# Run
.\build\Release\HelloCMake.exe
```

You should see: `Hello from CMake + MSVC!`

---

## Step 7 — VS Code Project Configuration

When you open a C++ project in VS Code, you need three configuration files in the `.vscode` folder. The AI tool can generate these for you, but here's what each one does:

### `.vscode/c_cpp_properties.json` — IntelliSense Configuration

Tells VS Code where to find the compiler and headers:

```json
{
    "configurations": [
        {
            "name": "Win32",
            "includePath": [
                "${workspaceFolder}/**"
            ],
            "defines": [
                "_DEBUG",
                "UNICODE",
                "_UNICODE"
            ],
            "windowsSdkVersion": "10.0.22621.0",
            "compilerPath": "cl.exe",
            "cStandard": "c17",
            "cppStandard": "c++17",
            "intelliSenseMode": "windows-msvc-x64"
        }
    ],
    "version": 4
}
```

### `.vscode/tasks.json` — Build Tasks

Tells VS Code how to compile your code:

```json
{
    "version": "2.0.0",
    "tasks": [
        {
            "type": "shell",
            "label": "C/C++: cl.exe build active file",
            "command": "cl.exe",
            "args": [
                "/Zi",
                "/EHsc",
                "/Fe:",
                "${fileDirname}\\${fileBasenameNoExtension}.exe",
                "${file}"
            ],
            "problemMatcher": ["$msCompile"],
            "group": {
                "kind": "build",
                "isDefault": true
            },
            "detail": "Compile with MSVC cl.exe"
        },
        {
            "type": "shell",
            "label": "CMake: Configure",
            "command": "cmake",
            "args": ["-B", "build", "-G", "Visual Studio 17 2022", "-A", "x64"],
            "problemMatcher": [],
            "detail": "Configure CMake project"
        },
        {
            "type": "shell",
            "label": "CMake: Build",
            "command": "cmake",
            "args": ["--build", "build", "--config", "Release"],
            "problemMatcher": ["$msCompile"],
            "group": "build",
            "detail": "Build CMake project"
        }
    ]
}
```

### `.vscode/launch.json` — Debug Configuration

Tells VS Code how to debug your program:

```json
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "C/C++: Debug with MSVC",
            "type": "cppvsdbg",
            "request": "launch",
            "program": "${workspaceFolder}\\build\\Release\\${fileBasenameNoExtension}.exe",
            "args": [],
            "stopAtEntry": false,
            "cwd": "${workspaceFolder}",
            "environment": [],
            "console": "integratedTerminal"
        }
    ]
}
```

> **Important:** VS Code must be launched from the Developer Command Prompt (or after running `Set-VsDevEnv`) for `cl.exe` to work in the integrated terminal.

---

## Step 8 — Building from PowerShell (Without VS Code)

You don't need VS Code open to compile. Here are the two main approaches:

### Single-File Compilation (cl.exe directly)

```powershell
# Load MSVC environment
Set-VsDevEnv

# Compile a single file
cl /EHsc main.cpp

# Compile with optimizations
cl /O2 /EHsc main.cpp

# Compile with debug info
cl /Zi /EHsc main.cpp

# Compile multiple files into one executable
cl /EHsc main.cpp utils.cpp /Fe:myapp.exe
```

### Multi-File Projects (CMake)

```powershell
# Load MSVC environment
Set-VsDevEnv

# Configure (only needed once, or when you change CMakeLists.txt)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release

# Build in Debug mode
cmake --build build --config Debug

# Clean and rebuild
cmake --build build --config Release --clean-first
```

### Common cl.exe Flags Reference

| Flag | Meaning |
|---|---|
| `/EHsc` | Enable C++ exception handling (always use this) |
| `/Zi` | Generate debug information (.pdb file) |
| `/O2` | Optimize for speed |
| `/Od` | Disable optimizations (for debugging) |
| `/Fe:<name>` | Name the output executable |
| `/Fo:<path>` | Name the output object file |
| `/I<dir>` | Add an include directory |
| `/std:c++17` | Use C++17 standard |
| `/std:c++20` | Use C++20 standard |
| `/W4` | Warning level 4 (recommended) |
| `/WX` | Treat warnings as errors |
| `/MT` | Static CRT linking (standalone .exe) |
| `/MD` | Dynamic CRT linking (default) |
| `/D<macro>` | Define a preprocessor macro |

---

## Complete Fresh Install Script

Copy and paste this entire block into an **Administrator PowerShell** window on a fresh system:

```powershell
# ============================================================
# C++ MSVC Development Environment — Fresh Install Script
# Run this in an Administrator PowerShell window
# ============================================================

# 1. Allow PowerShell scripts to run
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

# 2. Install Visual Studio Build Tools with C++ workload
#    (This takes 10-30 minutes and downloads ~6 GB)
winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"

# 3. Install CMake
winget install Kitware.CMake

# 4. Install Git
winget install Git.Git

# 5. Install VS Code
winget install Microsoft.VisualStudioCode

# 6. Install VS Code C++ extensions
code --install-extension ms-vscode.cpptools
code --install-extension ms-vscode.cmake-tools

Write-Host "`n========== Installation Complete ==========" -ForegroundColor Cyan
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Close and reopen PowerShell" -ForegroundColor White
Write-Host "2. Open 'Developer Command Prompt for VS' from Start menu" -ForegroundColor White
Write-Host "3. Run: cl" -ForegroundColor White
Write-Host "   (should print Microsoft C/C++ Compiler version)" -ForegroundColor White
Write-Host "4. Run: cmake --version" -ForegroundColor White
Write-Host "=============================================" -ForegroundColor Cyan
```

---

## Quick Reference — All Download Links

| Tool | Download Page | winget Command |
|---|---|---|
| **VS Build Tools** | [https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) | `winget install Microsoft.VisualStudio.2022.BuildTools --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"` |
| **VS Community** (free for individuals) | [https://visualstudio.microsoft.com/vs/community/](https://visualstudio.microsoft.com/vs/community/) | `winget install Microsoft.VisualStudio.2022.Community --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive"` |
| **CMake** | [https://cmake.org/download/](https://cmake.org/download/) | `winget install Kitware.CMake` |
| **Git** | [https://git-scm.com/download/win](https://git-scm.com/download/win) | `winget install Git.Git` |
| **VS Code** | [https://code.visualstudio.com](https://code.visualstudio.com) | `winget install Microsoft.VisualStudioCode` |
| **C/C++ Extension** | VS Code Marketplace | `code --install-extension ms-vscode.cpptools` |
| **CMake Tools Extension** | VS Code Marketplace | `code --install-extension ms-vscode.cmake-tools` |

---

## Troubleshooting

### `cl` is not recognized

You're not in a Developer Command Prompt. Either:
- Open **"Developer Command Prompt for VS"** from the Start menu, or
- Run `Set-VsDevEnv` in PowerShell (if you added the function from Step 5)

### `cmake` is not recognized

CMake wasn't added to PATH during install. Either:
- Reinstall CMake and check "Add to system PATH", or
- Add it manually: `C:\Program Files\CMake\bin` to your system PATH

### VS Code can't find `cl.exe`

VS Code must be launched from a Developer Command Prompt, or you must run `Set-VsDevEnv` before launching `code .`.

### "fatal error C1034: iostream: no include path"

The MSVC environment variables aren't loaded. Run `Set-VsDevEnv` or use the Developer Command Prompt.

### Build Tools license concerns

If you're building closed-source/commercial software and don't have a Visual Studio license, use **Visual Studio Community** instead of Build Tools. Community is free for individual developers.

---

## C++ vs Node.js vs Android — Setup Comparison

| Aspect | C++ (MSVC) | Node.js | Android |
|---|---|---|---|
| **Compiler/Runtime** | MSVC (cl.exe) | Node.js (V8) | JDK + Android SDK |
| **Download size** | ~6 GB (Build Tools) | ~80 MB | ~8-16 GB |
| **Setup time** | 15-30 min | 5 min | 30-60 min |
| **IDE required?** | No (VS Code is enough) | No | No |
| **Build from PowerShell?** | Yes (via Developer Cmd) | Yes | Yes (via Gradle) |
| **Special shell needed?** | Yes (Developer Cmd) | No | No |
| **Free for commercial use?** | Build Tools: needs VS license. Community: free for individuals | Yes | Yes |
| **Total disk space** | ~6-8 GB | ~200-500 MB | ~8-16 GB |

---

*Last updated: June 2026*
