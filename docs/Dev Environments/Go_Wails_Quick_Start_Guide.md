# Go + Wails Development - Clean System Setup Guide

> **Goal:** Starting from a fresh Windows machine, set up a complete Go development environment that can build command-line tools, APIs, services, and Wails desktop applications from PowerShell. The setup is written so an AI coding agent has enough tooling, conventions, and verification commands to safely program Go and Wails projects.

---

## What You'll End Up With

| Tool | What It Does | Installed By |
|---|---|---|
| **Go** | Compiler, standard library, module system, formatter, test runner | winget or official installer |
| **GOPATH/GOBIN PATH** | Location for installed Go CLI tools such as `wails`, `gopls`, and `dlv` | PowerShell environment configuration |
| **gopls** | Official Go language server for editor intelligence | `go install` |
| **Delve** | Go debugger | `go install` |
| **Staticcheck** | Extra static analysis for Go code | `go install` |
| **Node.js LTS + npm** | Required by Wails frontend templates | fnm or winget |
| **WebView2 Runtime** | Windows webview runtime used by Wails desktop apps | Usually built into Windows 10/11, otherwise Microsoft installer |
| **Wails CLI v3** | Creates, develops, checks, and builds new Wails v3 apps with `wails3` | `go install` |
| **Wails CLI v2** | Maintains existing Wails v2 apps with `wails` | `go install` |
| **NSIS** | Optional Windows installer builder for Wails | winget |
| **Git** | Version control | winget |
| **VS Code** | Editor with Go support | winget |

---

## Go Development Model for Agents

Modern Go development is module-first:

- `go.mod` defines the module path and Go version.
- `go.sum` locks dependency checksums.
- `GOPATH` is still used for the module cache and installed binaries, but normal project source code does not need to live under `GOPATH\src`.
- `go install package@version` installs command-line tools into `GOBIN`, or into `GOPATH\bin` when `GOBIN` is unset.
- `gofmt` is mandatory. Do not hand-format Go.

For application projects, commit both `go.mod` and `go.sum`.

For Wails, use v3 for new projects unless you have a compatibility reason to stay on v2. Wails v3 uses the `wails3` command and requires Go 1.25 or later. Wails v2 uses the `wails` command and is still common in existing repositories.

---

## Step 1 - Install Go

### Install via winget

```powershell
winget install GoLang.Go `
  --accept-source-agreements `
  --accept-package-agreements
```

Close and reopen PowerShell after installation.

### Or install manually

1. Go to [https://go.dev/dl/](https://go.dev/dl/).
2. Download the Windows `.msi` installer.
3. Run it with the default options.
4. Close and reopen PowerShell.

### Verify

```powershell
go version
go env GOROOT
go env GOPATH
go env GOBIN
```

The default Windows `GOPATH` is normally:

```text
%USERPROFILE%\go
```

---

## Step 2 - Configure GOPATH and PATH

Make sure installed Go tools are available from every PowerShell window.

```powershell
$goPath = Join-Path $env:USERPROFILE "go"
go env -w GOPATH="$goPath"

$goBin = Join-Path $goPath "bin"
New-Item -ItemType Directory -Force -Path $goBin | Out-Null

$currentUserPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($currentUserPath -notlike "*$goBin*") {
    [Environment]::SetEnvironmentVariable("Path", "$currentUserPath;$goBin", "User")
}
```

Close and reopen PowerShell, then verify:

```powershell
$env:Path -split ';' | Where-Object { $_ -like "*\go\bin*" }
go env GOPATH
```

---

## Step 3 - Install Git

```powershell
winget install Git.Git `
  --accept-source-agreements `
  --accept-package-agreements
```

Configure your identity:

```powershell
git config --global user.name "Your Name"
git config --global user.email "your.email@example.com"
git config --global init.defaultBranch main
```

Verify:

```powershell
git --version
```

---

## Step 4 - Install VS Code and Go Tooling

```powershell
winget install Microsoft.VisualStudioCode `
  --accept-source-agreements `
  --accept-package-agreements
```

Install VS Code extensions:

```powershell
code --install-extension golang.Go
code --install-extension tamasfe.even-better-toml
```

Install Go language tools:

```powershell
go install golang.org/x/tools/gopls@latest
go install github.com/go-delve/delve/cmd/dlv@latest
go install honnef.co/go/tools/cmd/staticcheck@latest
```

Verify:

```powershell
gopls version
dlv version
staticcheck -version
```

---

## Step 5 - Install Node.js for Wails Frontends

Wails uses frontend templates that depend on Node.js and npm. If you already followed the Node.js guide in this folder, skip this step.

```powershell
winget install Schniz.fnm `
  --accept-source-agreements `
  --accept-package-agreements

$profileDir = Split-Path $PROFILE
if (-not (Test-Path $profileDir)) { New-Item -ItemType Directory -Path $profileDir -Force }
$fnmLine = 'fnm env --use-on-cd --shell powershell | Out-String | Invoke-Expression'
if (Test-Path $PROFILE) {
    $content = Get-Content $PROFILE -Raw
    if ($content -notmatch 'fnm env') { Add-Content -Path $PROFILE -Value "`n$fnmLine" }
} else {
    Set-Content -Path $PROFILE -Value $fnmLine
}

fnm env --use-on-cd --shell powershell | Out-String | Invoke-Expression
fnm install --lts
fnm use --lts
fnm default lts-latest
```

Verify:

```powershell
node --version
npm --version
```

---

## Step 6 - Check or Install WebView2

Wails desktop apps on Windows require Microsoft Edge WebView2 Runtime. Windows 10/11 usually already has it.

Check:

```powershell
Get-ChildItem `
  -Path "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients" `
  -ErrorAction SilentlyContinue |
  Get-ItemProperty |
  Where-Object { $_.name -like "*WebView2*" } |
  Select-Object name, pv
```

If Wails reports WebView2 is missing, install the Evergreen Runtime from:

[https://developer.microsoft.com/microsoft-edge/webview2/](https://developer.microsoft.com/microsoft-edge/webview2/)

---

## Step 7 - Install Wails

Install Wails v3 for new projects:

```powershell
go install github.com/wailsapp/wails/v3/cmd/wails3@latest
```

Close and reopen PowerShell if `wails3` is not recognized, then verify:

```powershell
wails3 doctor
wails3 init -l
```

`wails3 doctor` checks the local dependencies and reports missing pieces.

### Optional: Install Wails v2 for Existing Projects

Many current Wails repositories still use v2. Install the v2 CLI when a project has `github.com/wailsapp/wails/v2` in `go.mod`, a `wails.json` file, or scripts that call `wails`.

```powershell
go install github.com/wailsapp/wails/v2/cmd/wails@latest
```

Verify:

```powershell
wails version
wails doctor
```

### Optional: Install NSIS for Windows Installers

Wails v2 can generate Windows installers with NSIS. For Wails v3, check the current packaging guide before adding installer requirements to a project.

```powershell
winget install NSIS.NSIS --silent `
  --accept-source-agreements `
  --accept-package-agreements
```

Verify:

```powershell
makensis /VERSION
```

---

## Step 8 - Create and Verify a Go Project

```powershell
mkdir C:\Code
cd C:\Code

mkdir hello-go
cd hello-go
go mod init example.com/hello-go
```

Create `main.go`:

```go
package main

import "fmt"

func main() {
	fmt.Println("Hello from Go!")
}
```

Run:

```powershell
go run .
go test ./...
gofmt -w .
go vet ./...
staticcheck ./...
```

Build:

```powershell
go build -o .\bin\hello-go.exe .
.\bin\hello-go.exe
```

---

## Step 9 - Create a Wails App

### Recommended Starter: Wails v3 Vanilla

This is the simplest baseline for an AI agent because it has fewer framework conventions to infer:

```powershell
cd C:\Code
wails3 init -n my-wails-app -t vanilla
cd my-wails-app
wails3 dev
```

Build a production binary:

```powershell
wails3 build
```

List current templates:

```powershell
wails3 init -l
```

Common Wails v3 templates:

```powershell
wails3 init -n app-react -t react
wails3 init -n app-vue -t vue
wails3 init -n app-svelte -t svelte
```

### Maintaining a Wails v2 App

Use v2 commands when the repository already has `wails.json` and `github.com/wailsapp/wails/v2` in `go.mod`.

```powershell
wails init -n my-wails-v2-app -t vanilla-ts
cd my-wails-v2-app
wails dev
wails build
wails build -nsis
```

---

## Step 10 - Understand the Wails Project Layout

Typical Wails v3 project:

```text
my-wails-app/
  build/
    config.yml
    Taskfile.yml
    appicon.png
  frontend/
    package.json
    src/
    bindings/
  go.mod
  go.sum
  greetservice.go
  main.go
  Taskfile.yml
```

Typical Wails v2 project:

```text
my-wails-app/
  build/
    appicon.png
    windows/
  frontend/
    package.json
    src/
  app.go
  go.mod
  go.sum
  main.go
  wails.json
```

| Path | What the Agent Should Know |
|---|---|
| `go.mod` | Module path, Go version, Go dependencies |
| `go.sum` | Dependency checksum lock file |
| `main.go` | Wails application startup, options, services, or asset server |
| `greetservice.go` or service files | Wails v3 services exposed to the frontend |
| `app.go` | Common Wails v2 application struct and methods exposed to the frontend |
| `frontend/` | Frontend app source and package manager files |
| `frontend/bindings/` | Wails v3 generated frontend bindings |
| `frontend/wailsjs/` | Wails v2 generated frontend bindings |
| `wails.json` | Wails v2 project configuration, build hooks, frontend install/build commands |
| `Taskfile.yml` | Wails v3 task runner entry point |
| `build/` | Icons, platform resources, installer configuration |

### Wails v3 Binding Pattern

Wails v3 exposes exported methods on registered service structs to the frontend.

Go service:

```go
package main

import "fmt"

type GreetService struct{}

func (g *GreetService) Greet(name string) string {
	return fmt.Sprintf("Hello %s!", name)
}
```

Service registration:

```go
app := application.New(application.Options{
	Name: "my-wails-app",
	Services: []application.Service{
		application.NewService(&GreetService{}),
	},
})
```

Frontend JavaScript or TypeScript:

```ts
import { GreetService } from "../bindings/changeme";

const message = await GreetService.Greet("Wails");
```

When Go service signatures change, Wails v3 regenerates bindings during `wails3 dev` and `wails3 build`. Do not edit generated binding files by hand.

### Wails v2 Binding Pattern

Wails v2 exposes exported methods on bound Go structs to the frontend.

Go:

```go
package main

import "context"

type App struct {
	ctx context.Context
}

func NewApp() *App {
	return &App{}
}

func (a *App) startup(ctx context.Context) {
	a.ctx = ctx
}

func (a *App) Greet(name string) string {
	return "Hello " + name + "!"
}
```

Frontend TypeScript:

```ts
import { Greet } from "../wailsjs/go/main/App";

const message = await Greet("Wails");
```

When Go method signatures change, Wails v2 regenerates frontend bindings during `wails dev` and `wails build`. Do not edit generated binding files by hand.

---

## Agent Programming Contract for Go and Wails

When an LLM coding agent works in a Go or Wails project, it should follow this contract.

### Before Editing

```powershell
go version
go env
go list ./...
```

For Wails v3 projects:

```powershell
wails3 doctor
node --version
npm --version
```

For Wails v2 projects:

```powershell
wails doctor
node --version
npm --version
```

Read whichever of these files and directories exist before changing behavior:

```text
go.mod
go.sum
README.md
main.go
greetservice.go
app.go
wails.json
Taskfile.yml
frontend/package.json
frontend/src/
frontend/bindings/
frontend/wailsjs/
```

### During Coding

- Use `go fmt` or `gofmt`; never hand-align formatting.
- Keep package names short, lowercase, and meaningful.
- Keep exported names documented when they are part of a package API.
- Return `error` values for fallible operations; do not panic for expected failures.
- Pass `context.Context` through long-running, I/O, network, and cancellation-aware paths.
- Keep UI-facing Wails methods small; move business logic into testable Go packages.
- Avoid global mutable state unless it is protected and clearly owned.
- Keep frontend build commands in `wails.json`, `Taskfile.yml`, and `frontend/package.json` synchronized with the Wails version in use.
- Prefer small interfaces at package boundaries, not large pre-emptive abstractions.

### Standard Verification

Run from the Go module root:

```powershell
gofmt -w .
go test ./...
go vet ./...
staticcheck ./...
go build ./...
```

Run from a Wails v3 project root:

```powershell
wails3 doctor
wails3 dev
wails3 build
```

Run from a Wails v2 project root:

```powershell
wails doctor
wails dev
wails build
```

For frontend changes:

```powershell
cd frontend
npm install
npm run build
```

### Done Criteria

A Go/Wails change is not complete until:

- `gofmt` has run.
- `go test ./...` passes.
- `go vet ./...` passes.
- `staticcheck ./...` passes when available.
- Wails dev mode opens successfully for UI changes.
- Wails build succeeds for packaging or desktop integration changes.
- Generated Wails bindings are updated when exported Go methods or services changed.
- README or project docs mention any new setup requirement.

---

## Recommended Go Project Defaults

### `go.mod`

```go
module example.com/my-app

go 1.26
```

Use the Go version installed on the machine or required by the project. Wails v3 requires Go 1.25 or later. Do not upgrade the `go` directive in an existing project without running the full test suite.

### `.golangci.yml` Optional Baseline

If the project uses `golangci-lint`, this is a moderate starting point:

```yaml
run:
  timeout: 5m

linters:
  enable:
    - govet
    - staticcheck
    - ineffassign
    - misspell

issues:
  max-issues-per-linter: 0
  max-same-issues: 0
```

Install it only when the project wants a single lint runner:

```powershell
go install github.com/golangci/golangci-lint/cmd/golangci-lint@latest
golangci-lint run ./...
```

### VS Code Workspace Settings

Create `.vscode/settings.json` when the repository does not already define one:

```json
{
  "go.useLanguageServer": true,
  "go.formatTool": "gofmt",
  "go.lintTool": "staticcheck",
  "go.testFlags": ["-count=1"],
  "editor.formatOnSave": true,
  "[go]": {
    "editor.defaultFormatter": "golang.go"
  }
}
```

---

## Complete Fresh Install Script

Run this in an **Administrator PowerShell** window on a fresh Windows machine:

```powershell
# ============================================================
# Go + Wails Development Environment - Fresh Install Script
# Run in Administrator PowerShell
# ============================================================

Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

# 1. Install Go, Git, VS Code, and Node version manager.
winget install GoLang.Go `
  --accept-source-agreements `
  --accept-package-agreements
winget install Git.Git `
  --accept-source-agreements `
  --accept-package-agreements
winget install Microsoft.VisualStudioCode `
  --accept-source-agreements `
  --accept-package-agreements
winget install Schniz.fnm `
  --accept-source-agreements `
  --accept-package-agreements

# 2. Configure fnm and install Node.js LTS for Wails frontends.
$profileDir = Split-Path $PROFILE
if (-not (Test-Path $profileDir)) { New-Item -ItemType Directory -Path $profileDir -Force }
$fnmLine = 'fnm env --use-on-cd --shell powershell | Out-String | Invoke-Expression'
if (Test-Path $PROFILE) {
    $content = Get-Content $PROFILE -Raw
    if ($content -notmatch 'fnm env') { Add-Content -Path $PROFILE -Value "`n$fnmLine" }
} else {
    Set-Content -Path $PROFILE -Value $fnmLine
}

fnm env --use-on-cd --shell powershell | Out-String | Invoke-Expression
fnm install --lts
fnm use --lts
fnm default lts-latest

# 3. Configure GOPATH and PATH.
$goPath = Join-Path $env:USERPROFILE "go"
go env -w GOPATH="$goPath"
$goBin = Join-Path $goPath "bin"
New-Item -ItemType Directory -Force -Path $goBin | Out-Null

$currentUserPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($currentUserPath -notlike "*$goBin*") {
    [Environment]::SetEnvironmentVariable("Path", "$currentUserPath;$goBin", "User")
}
$env:Path = "$env:Path;$goBin"

# 4. Install Go tools and Wails.
go install golang.org/x/tools/gopls@latest
go install github.com/go-delve/delve/cmd/dlv@latest
go install honnef.co/go/tools/cmd/staticcheck@latest
go install github.com/wailsapp/wails/v3/cmd/wails3@latest
go install github.com/wailsapp/wails/v2/cmd/wails@latest

# 5. Install optional NSIS for Wails v2 Windows installers.
winget install NSIS.NSIS --silent `
  --accept-source-agreements `
  --accept-package-agreements

# 6. Install VS Code extensions.
code --install-extension golang.Go
code --install-extension tamasfe.even-better-toml

# 7. Verify.
Write-Host "`n========== Go + Wails Installation Summary ==========" -ForegroundColor Cyan
Write-Host "Go:          $(go version)" -ForegroundColor Green
Write-Host "GOPATH:      $(go env GOPATH)" -ForegroundColor Green
Write-Host "Node:        $(node --version)" -ForegroundColor Green
Write-Host "npm:         $(npm --version)" -ForegroundColor Green
Write-Host "Git:         $(git --version)" -ForegroundColor Green
Write-Host "gopls:       $(gopls version)" -ForegroundColor Green
Write-Host "Delve:       $(dlv version | Select-Object -First 1)" -ForegroundColor Green
Write-Host "Staticcheck: $(staticcheck -version)" -ForegroundColor Green
Write-Host "Wails v3:" -ForegroundColor Green
wails3 doctor
Write-Host "Wails v2:" -ForegroundColor Green
wails version
Write-Host "=====================================================" -ForegroundColor Cyan
Write-Host "`nClose and reopen PowerShell, then run: wails3 doctor" -ForegroundColor Yellow
```

---

## Quick Reference - Daily Commands

| Task | Command |
|---|---|
| Show Go version | `go version` |
| Show Go environment | `go env` |
| Create module | `go mod init example.com/my-app` |
| Add or update dependency | `go get example.com/package@latest` |
| Tidy dependencies | `go mod tidy` |
| Run app | `go run .` |
| Run tests | `go test ./...` |
| Format | `gofmt -w .` |
| Vet | `go vet ./...` |
| Static analysis | `staticcheck ./...` |
| Build | `go build ./...` |
| Install current command | `go install .` |
| Install external command | `go install module/path/cmd/tool@latest` |
| Create Wails v3 app | `wails3 init -n my-app -t vanilla` |
| Check Wails v3 setup | `wails3 doctor` |
| Run Wails v3 dev | `wails3 dev` |
| Build Wails v3 app | `wails3 build` |
| Create Wails v2 app | `wails init -n my-app -t vanilla-ts` |
| Check Wails v2 setup | `wails doctor` |
| Run Wails v2 dev | `wails dev` |
| Build Wails v2 app | `wails build` |
| Build Wails v2 NSIS installer | `wails build -nsis` |

---

## Troubleshooting

### `go` is not recognized

Close and reopen PowerShell. If it still fails, reinstall Go from [https://go.dev/dl/](https://go.dev/dl/) or with winget:

```powershell
winget install GoLang.Go
```

### `wails3` or `wails` is not recognized

The Go binary directory is missing from PATH. Check:

```powershell
go env GOPATH
$env:Path -split ';' | Where-Object { $_ -like "*\go\bin*" }
```

Add it:

```powershell
$goBin = Join-Path (go env GOPATH) "bin"
$currentUserPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($currentUserPath -notlike "*$goBin*") {
    [Environment]::SetEnvironmentVariable("Path", "$currentUserPath;$goBin", "User")
}
```

Close and reopen PowerShell.

### `wails3 doctor` or `wails doctor` says Node or npm is missing

Install Node.js LTS using the Node.js guide in this folder, or run the fnm setup in Step 5.

### `wails3 doctor` or `wails doctor` says WebView2 is missing

Install the Evergreen WebView2 Runtime from:

[https://developer.microsoft.com/microsoft-edge/webview2/](https://developer.microsoft.com/microsoft-edge/webview2/)

### Frontend build fails during `wails3 dev` or `wails dev`

Build the frontend directly:

```powershell
cd frontend
npm install
npm run build
```

Fix frontend errors before debugging Go or Wails.

### Go dependency state looks wrong

Run:

```powershell
go mod tidy
go clean -modcache
go mod download
go test ./...
```

Use `go clean -modcache` sparingly because it forces dependencies to be downloaded again.

### Wails bindings are stale

Restart Wails dev mode:

```powershell
Ctrl+C
# Wails v3:
wails3 dev

# Wails v2:
wails dev
```

If generated files are still stale, remove the generated binding directory for the project version and rerun dev mode:

```text
frontend/bindings/   # Wails v3
frontend/wailsjs/    # Wails v2
```

---

## Official References

| Topic | Link |
|---|---|
| Go downloads | [https://go.dev/dl/](https://go.dev/dl/) |
| Go install guide | [https://go.dev/doc/install](https://go.dev/doc/install) |
| GOPATH wiki | [https://go.dev/wiki/GOPATH](https://go.dev/wiki/GOPATH) |
| Setting GOPATH | [https://go.dev/wiki/SettingGOPATH](https://go.dev/wiki/SettingGOPATH) |
| Go release history | [https://go.dev/doc/devel/release](https://go.dev/doc/devel/release) |
| Wails v3 installation | [https://v3.wails.io/quick-start/installation/](https://v3.wails.io/quick-start/installation/) |
| Wails v3 first app | [https://v3.wails.io/quick-start/first-app/](https://v3.wails.io/quick-start/first-app/) |
| Wails v3 CLI reference | [https://v3.wails.io/guides/cli/](https://v3.wails.io/guides/cli/) |
| Wails v2 installation | [https://wails.io/docs/gettingstarted/installation/](https://wails.io/docs/gettingstarted/installation/) |
| Wails v2 first project | [https://wails.io/docs/gettingstarted/firstproject/](https://wails.io/docs/gettingstarted/firstproject/) |
| Wails v2 development | [https://wails.io/docs/gettingstarted/development/](https://wails.io/docs/gettingstarted/development/) |
| Wails v2 build | [https://wails.io/docs/gettingstarted/building/](https://wails.io/docs/gettingstarted/building/) |
| Wails v2 NSIS installer | [https://wails.io/docs/guides/windows-installer/](https://wails.io/docs/guides/windows-installer/) |
| WebView2 Runtime | [https://developer.microsoft.com/microsoft-edge/webview2/](https://developer.microsoft.com/microsoft-edge/webview2/) |

---

*Last updated: June 2026*
