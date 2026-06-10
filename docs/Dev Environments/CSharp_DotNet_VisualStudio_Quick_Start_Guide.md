# C# + .NET + Visual Studio Development - Clean System Setup Guide

> **Goal:** Starting from a fresh Windows machine, set up a complete C#/.NET development environment that can build console apps, libraries, web APIs, desktop apps, and visual UI projects. The guide includes both command-line setup for an AI coding agent and Visual Studio Community setup for visual designers such as WinForms, WPF, WinUI, and .NET MAUI.

---

## What You'll End Up With

| Tool | What It Does | Installed By |
|---|---|---|
| **.NET SDK 10** | Current LTS .NET SDK, compiler, CLI, templates, MSBuild | winget |
| **C# compiler** | Builds C# projects through `dotnet build` and MSBuild | .NET SDK |
| **NuGet** | Package restore and dependency management | .NET SDK / Visual Studio |
| **Visual Studio Community** | Full IDE with designers, debugging, profiling, publishing | winget or browser |
| **.NET desktop workload** | WinForms, WPF, console, and .NET Framework tooling | Visual Studio workload |
| **ASP.NET workload** | Web apps, APIs, Razor, publishing tools | Optional Visual Studio workload |
| **WinUI workload** | Windows App SDK / WinUI project support | Optional Visual Studio workload |
| **.NET MAUI workload** | Cross-platform C# UI for Windows and Android from Windows | Optional Visual Studio workload or `dotnet workload` |
| **VS Code** | Lightweight editor for CLI-first C# work | winget |
| **Git** | Version control | winget |

---

## Choose an Install Path

| Path | Best For | Includes Visual Designers |
|---|---|---|
| **A. CLI-only .NET SDK** | Agents, CI, console apps, libraries, services | No |
| **B. Visual Studio Community - desktop** | WinForms/WPF visual editing, most Windows C# apps | Yes |
| **C. Visual Studio Community - expanded** | Desktop, web, WinUI, and MAUI projects | Yes |
| **D. Browser install** | Humans who want to select workloads manually | Yes |

For an AI coding agent, install Path A at minimum. Use Path B or C when the project needs visual designers or Visual Studio-only tooling.

---

## Visual Studio Community Licensing Note

Visual Studio Community is free for individual developers, students, open-source contributors, academic use, and non-enterprise teams that meet Microsoft's Community license limits. If you are inside an enterprise or commercial organization, confirm the license before using Community.

.NET itself is free and open source with no licensing cost.

---

## Path A - CLI-Only .NET SDK Install

Run this in an Administrator PowerShell window:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

winget install --id Microsoft.DotNet.SDK.10 --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements

winget install --id Git.Git --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements

winget install --id Microsoft.VisualStudioCode --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements

code --install-extension ms-dotnettools.csdevkit
code --install-extension ms-dotnettools.csharp
code --install-extension ms-dotnettools.vscode-dotnet-runtime
```

Close and reopen PowerShell if `dotnet` is not recognized.

Verify:

```powershell
dotnet --version
dotnet --list-sdks
dotnet --info
git --version
```

---

## Path B - Visual Studio Community for C# Desktop Apps

This is the recommended Visual Studio install for editing the visual side of C# Windows apps. It installs Visual Studio Community with the **.NET desktop development** workload, which supports Windows Forms, WPF, console apps, class libraries, C#, Visual Basic, F#, .NET, and .NET Framework.

Run this in an Administrator PowerShell window:

```powershell
winget install --id Microsoft.VisualStudio.Community --exact `
  --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.ManagedDesktop --includeRecommended" `
  --accept-source-agreements `
  --accept-package-agreements
```

If a repository specifically requires Visual Studio 2022 instead of the latest Community channel, use:

```powershell
winget install --id Microsoft.VisualStudio.2022.Community --exact `
  --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.ManagedDesktop --includeRecommended" `
  --accept-source-agreements `
  --accept-package-agreements
```

Visual Studio installer operations require administrator privileges and may return success with a reboot-required code. If designers or build tools behave strangely after install, restart Windows once.

---

## Path C - Expanded Visual Studio Community Install

Use this when you want one machine ready for C# desktop, web, WinUI, and MAUI work. It is larger than Path B.

```powershell
winget install --id Microsoft.VisualStudio.Community --exact `
  --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.ManagedDesktop --add Microsoft.VisualStudio.Workload.NetWeb --add Microsoft.VisualStudio.Workload.Universal --add Microsoft.VisualStudio.Workload.NetCrossPlat --includeRecommended" `
  --accept-source-agreements `
  --accept-package-agreements
```

Workload meanings:

| Workload ID | What It Adds |
|---|---|
| `Microsoft.VisualStudio.Workload.ManagedDesktop` | C#/.NET desktop apps, WinForms, WPF, .NET Framework |
| `Microsoft.VisualStudio.Workload.NetWeb` | ASP.NET Core, web apps, APIs, Razor, IIS Express tooling |
| `Microsoft.VisualStudio.Workload.Universal` | WinUI / Windows App SDK tooling |
| `Microsoft.VisualStudio.Workload.NetCrossPlat` | .NET MAUI cross-platform tooling |

For MAUI CLI support outside Visual Studio:

```powershell
dotnet workload install maui
dotnet workload list
```

On Windows, MAUI can target Windows and Android. Android builds also need Android SDK/JDK/emulator setup; use the Android guide in this folder when needed.

---

## Path D - Browser Install

Official Visual Studio Community page:

[https://visualstudio.microsoft.com/vs/community/](https://visualstudio.microsoft.com/vs/community/)

1. Download Visual Studio Community.
2. Run the installer.
3. Select **.NET desktop development** for WinForms/WPF/desktop apps.
4. Add **ASP.NET and web development** for web apps and APIs.
5. Add **WinUI application development** for Windows App SDK / WinUI apps.
6. Add **.NET Multi-platform App UI development** for .NET MAUI apps.
7. Click Install.

---

## Agent-Friendly Fresh Install Script

Run this in an Administrator PowerShell window when a model should prepare a practical C#/.NET machine with command-line tooling and Visual Studio visual designers:

```powershell
# ============================================================
# C# + .NET + Visual Studio Community - Fresh Install Script
# Run in Administrator PowerShell
# ============================================================

Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

# 1. Install .NET SDK 10 LTS, Git, and VS Code.
winget install --id Microsoft.DotNet.SDK.10 --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
winget install --id Git.Git --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
winget install --id Microsoft.VisualStudioCode --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements

# 2. Install VS Code C# tooling.
code --install-extension ms-dotnettools.csdevkit
code --install-extension ms-dotnettools.csharp
code --install-extension ms-dotnettools.vscode-dotnet-runtime

# 3. Install Visual Studio Community with .NET desktop workload.
winget install --id Microsoft.VisualStudio.Community --exact `
  --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.ManagedDesktop --includeRecommended" `
  --accept-source-agreements `
  --accept-package-agreements

# 4. Verify .NET CLI. Close and reopen PowerShell if dotnet is not recognized.
Write-Host "`n========== C#/.NET Installation Summary ==========" -ForegroundColor Cyan
dotnet --version
dotnet --list-sdks
dotnet --info
git --version
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "`nOpen Visual Studio Community from the Start menu for visual designers." -ForegroundColor Yellow
```

---

## Create and Verify C# Projects from PowerShell

### Console App

```powershell
mkdir C:\Code
cd C:\Code

dotnet new console -n HelloCSharp
cd HelloCSharp
dotnet run
dotnet build
```

### Class Library and Test Project

```powershell
cd C:\Code
mkdir CSharpSolution
cd CSharpSolution

dotnet new sln -n CSharpSolution
dotnet new classlib -n MyLibrary
dotnet new xunit -n MyLibrary.Tests

dotnet sln add .\MyLibrary\MyLibrary.csproj
dotnet sln add .\MyLibrary.Tests\MyLibrary.Tests.csproj
dotnet add .\MyLibrary.Tests\MyLibrary.Tests.csproj reference .\MyLibrary\MyLibrary.csproj

dotnet restore
dotnet build
dotnet test
```

### Windows Forms App

```powershell
cd C:\Code
dotnet new winforms -n MyWinFormsApp
cd MyWinFormsApp
dotnet build
dotnet run
```

Open `MyWinFormsApp.csproj` or a `.sln` file in Visual Studio Community to use the WinForms designer.

### WPF App

```powershell
cd C:\Code
dotnet new wpf -n MyWpfApp
cd MyWpfApp
dotnet build
dotnet run
```

Open the project in Visual Studio Community to use XAML designer, XAML editor, property grids, resources, and event wiring.

### ASP.NET Core Web API

```powershell
cd C:\Code
dotnet new webapi -n MyApi
cd MyApi
dotnet run
```

The terminal prints the local HTTPS/HTTP URLs.

### .NET MAUI App

Install the MAUI workload first:

```powershell
dotnet workload install maui
dotnet workload list
```

Create and build:

```powershell
cd C:\Code
dotnet new maui -n MyMauiApp
cd MyMauiApp
dotnet build
```

For Android targets, complete the Android SDK setup from the Android guide in this folder.

---

## Visual Studio Designer Guidance

Use Visual Studio Community when the project relies on visual tooling:

| Project Type | Visual Tooling |
|---|---|
| **Windows Forms** | Drag-and-drop forms designer, controls, property grid, event handlers |
| **WPF** | XAML editor/designer, bindings, resource dictionaries, styles |
| **WinUI** | XAML tooling for Windows App SDK apps |
| **.NET MAUI** | XAML tooling, hot reload, platform target selection |
| **ASP.NET/Razor** | Razor tooling, scaffolding, publish profiles |

Agent rules:

- Do not casually hand-edit `*.Designer.cs`; it is generated by the WinForms designer.
- For WPF/WinUI/MAUI, keep `.xaml` and `.xaml.cs` code-behind synchronized.
- Prefer MVVM for WPF, WinUI, and MAUI when the project already uses it.
- If a UI is designer-owned, make structural changes in Visual Studio when practical and use code edits for logic.
- Keep generated files committed only when the project already commits them.

---

## Recommended Project Configuration

### `global.json`

Use `global.json` when the repository must pin a .NET SDK:

```json
{
  "sdk": {
    "version": "10.0.100",
    "rollForward": "latestFeature"
  }
}
```

Do not add or update `global.json` in an existing repository without checking the installed SDKs and project requirements.

### `.editorconfig`

A practical C# baseline:

```ini
root = true

[*.cs]
indent_style = space
indent_size = 4
charset = utf-8-bom
end_of_line = crlf
insert_final_newline = true

dotnet_sort_system_directives_first = true
dotnet_style_qualification_for_field = false:suggestion
dotnet_style_qualification_for_property = false:suggestion
dotnet_style_qualification_for_method = false:suggestion
dotnet_style_qualification_for_event = false:suggestion

csharp_style_var_for_built_in_types = true:suggestion
csharp_style_var_when_type_is_apparent = true:suggestion
csharp_style_var_elsewhere = false:suggestion
```

### `Directory.Build.props`

For a new solution, this keeps common build settings in one place:

```xml
<Project>
  <PropertyGroup>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <TreatWarningsAsErrors>false</TreatWarningsAsErrors>
    <AnalysisLevel>latest</AnalysisLevel>
  </PropertyGroup>
</Project>
```

Turn `TreatWarningsAsErrors` on only when the repository is ready for that strictness.

---

## Agent Programming Contract for C#/.NET

Before editing:

```powershell
dotnet --version
dotnet --list-sdks
dotnet --info
dotnet workload list
dotnet restore
```

Read whichever of these files exist:

```text
*.sln
*.csproj
global.json
Directory.Build.props
Directory.Build.targets
NuGet.config
.editorconfig
appsettings.json
appsettings.Development.json
Program.cs
Startup.cs
*.xaml
*.xaml.cs
*.Designer.cs
```

During coding:

- Prefer `dotnet add package` and `dotnet remove package` for dependencies.
- Keep nullable reference types enabled when the project uses them.
- Use async APIs for I/O and long-running work.
- Keep UI thread work small; use async commands/background work for UI apps.
- Do not change target frameworks casually.
- Preserve existing solution/project layout and naming conventions.
- Do not manually rewrite generated designer files unless there is no designer-safe alternative.

Standard verification:

```powershell
dotnet restore
dotnet build --no-restore
dotnet test --no-build       # if the solution has test projects
dotnet format --verify-no-changes
```

For desktop apps:

```powershell
dotnet run --project .\Path\To\App.csproj
```

For MAUI:

```powershell
dotnet workload list
dotnet build
```

Done criteria:

- Restore succeeds.
- Build succeeds.
- Tests pass when present.
- Formatting/analyzers pass or exceptions are explained.
- UI designer files and code-behind remain consistent.
- New packages, SDK requirements, workloads, or target frameworks are documented.

---

## Quick Reference - Daily Commands

| Task | Command |
|---|---|
| Show SDK version | `dotnet --version` |
| Show installed SDKs | `dotnet --list-sdks` |
| Show environment | `dotnet --info` |
| List templates | `dotnet new list` |
| Create console app | `dotnet new console -n MyApp` |
| Create class library | `dotnet new classlib -n MyLibrary` |
| Create test project | `dotnet new xunit -n MyTests` |
| Create WinForms app | `dotnet new winforms -n MyWinFormsApp` |
| Create WPF app | `dotnet new wpf -n MyWpfApp` |
| Create Web API | `dotnet new webapi -n MyApi` |
| Create solution | `dotnet new sln -n MySolution` |
| Add project to solution | `dotnet sln add .\MyApp\MyApp.csproj` |
| Add package | `dotnet add package Package.Name` |
| Restore | `dotnet restore` |
| Build | `dotnet build` |
| Test | `dotnet test` |
| Run | `dotnet run` |
| Format | `dotnet format` |
| Install MAUI workload | `dotnet workload install maui` |
| List workloads | `dotnet workload list` |
| Clear NuGet caches | `dotnet nuget locals all --clear` |

---

## Troubleshooting

### `dotnet` is not recognized

Close and reopen PowerShell. If it still fails, check whether the .NET install directory is on PATH:

```powershell
$env:Path -split ';' | Where-Object { $_ -like "*\dotnet*" }
```

The default install path is:

```text
C:\Program Files\dotnet
```

Add it to the current session if needed:

```powershell
$dotnetPath = "${env:ProgramFiles}\dotnet"
if ($env:Path -notlike "*$dotnetPath*") {
    $env:Path = "$env:Path;$dotnetPath"
}
dotnet --version
```

### Visual Studio install says reboot required

Microsoft's installer can complete successfully but require a reboot before all components work. Restart Windows once, then open Visual Studio Installer and verify the workloads are installed.

### WinForms or WPF designer is missing

Open Visual Studio Installer, choose **Modify**, and confirm **.NET desktop development** is installed. From PowerShell:

```powershell
winget install --id Microsoft.VisualStudio.Community --exact `
  --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.ManagedDesktop --includeRecommended"
```

### ASP.NET templates are missing

Install the web workload:

```powershell
winget install --id Microsoft.VisualStudio.Community --exact `
  --override "--wait --passive --norestart --add Microsoft.VisualStudio.Workload.NetWeb --includeRecommended"
```

Then verify templates:

```powershell
dotnet new list web
```

### MAUI templates are missing

Install the MAUI workload:

```powershell
dotnet workload install maui
dotnet workload list
dotnet new list maui
```

If building Android targets, confirm Android SDK setup from the Android guide.

### NuGet restore fails

Clear caches and restore again:

```powershell
dotnet nuget locals all --clear
dotnet restore
```

If the project uses private feeds, inspect `NuGet.config` before changing package sources.

---

## Official References

| Topic | Link |
|---|---|
| Install .NET on Windows | [https://learn.microsoft.com/dotnet/core/install/windows](https://learn.microsoft.com/dotnet/core/install/windows) |
| .NET downloads | [https://dotnet.microsoft.com/download](https://dotnet.microsoft.com/download) |
| Visual Studio Community | [https://visualstudio.microsoft.com/vs/community/](https://visualstudio.microsoft.com/vs/community/) |
| Visual Studio command-line install | [https://learn.microsoft.com/visualstudio/install/use-command-line-parameters-to-install-visual-studio](https://learn.microsoft.com/visualstudio/install/use-command-line-parameters-to-install-visual-studio) |
| Visual Studio workload IDs | [https://learn.microsoft.com/visualstudio/install/workload-component-id-vs-community](https://learn.microsoft.com/visualstudio/install/workload-component-id-vs-community) |
| .NET MAUI installation | [https://learn.microsoft.com/dotnet/maui/get-started/installation](https://learn.microsoft.com/dotnet/maui/get-started/installation) |
| Visual Studio licensing guidance | [https://www.microsoft.com/licensing/guidance/Visual-Studio](https://www.microsoft.com/licensing/guidance/Visual-Studio) |

---

*Last updated: June 2026*
