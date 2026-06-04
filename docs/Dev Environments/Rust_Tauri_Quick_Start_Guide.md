# Rust + Tauri Development - Clean System Setup Guide

> **Goal:** Starting from a fresh Windows machine, set up a complete Rust development environment that can build command-line tools, libraries, and Tauri desktop applications from PowerShell. The setup is written so an AI coding agent has enough tooling, conventions, and verification commands to safely program Rust and Tauri projects.

---

## What You'll End Up With

| Tool | What It Does | Installed By |
|---|---|---|
| **Microsoft C++ Build Tools** | MSVC linker, Windows SDK, and native libraries required by Rust MSVC and Tauri on Windows | winget or Visual Studio Installer |
| **WebView2 Runtime** | Windows webview runtime used by Tauri desktop apps | Usually built into Windows 10/11, otherwise Microsoft installer |
| **rustup** | Rust toolchain manager | winget or rustup-init |
| **Rust stable-msvc** | Rust compiler and standard library for the MSVC Windows target | rustup |
| **Cargo** | Rust package manager, build tool, test runner, and project scaffold tool | rustup |
| **rustfmt** | Official Rust formatter | rustup component |
| **Clippy** | Official Rust linter | rustup component |
| **rust-src** | Local Rust standard library sources for editor navigation and macro expansion | rustup component |
| **Node.js LTS + npm** | Required for Tauri apps that use JavaScript or TypeScript frontends | fnm or winget |
| **Tauri CLI** | Creates, develops, and builds Tauri applications | npm dev dependency or cargo install |
| **Git** | Version control | winget |
| **VS Code** | Editor with Rust/Tauri extensions | winget |

---

## Important Windows Choice - Use MSVC Rust

On Windows, Rust can target either MSVC or GNU. For Tauri and normal Windows desktop development, use the MSVC toolchain:

```powershell
rustup default stable-msvc
```

The expected default host triple on most modern Windows PCs is:

```text
x86_64-pc-windows-msvc
```

Use GNU only when a project explicitly requires MinGW. Mixing GNU and MSVC dependencies is a common source of linker errors.

---

## Step 1 - Install Windows Native Build Dependencies

Rust itself can install quickly, but Windows Rust projects often need a linker and Windows SDK. Tauri also requires Microsoft C++ Build Tools and WebView2.

### Option A: Install Visual Studio Build Tools

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools `
  --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive" `
  --accept-source-agreements `
  --accept-package-agreements
```

If you use the GUI installer instead:

1. Download Build Tools for Visual Studio from [https://visualstudio.microsoft.com/visual-cpp-build-tools/](https://visualstudio.microsoft.com/visual-cpp-build-tools/).
2. Select **Desktop development with C++**.
3. Keep the recommended MSVC toolset, Windows SDK, and CMake tools selected.
4. Install and restart PowerShell.

### Option B: Install Visual Studio Community

If you want the full IDE as well:

```powershell
winget install Microsoft.VisualStudio.2022.Community `
  --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive" `
  --accept-source-agreements `
  --accept-package-agreements
```

### Verify MSVC Tools

Open **Developer PowerShell for VS 2022** from the Start menu, then run:

```powershell
cl
where link
```

`cl` should print the Microsoft compiler banner. A regular PowerShell window may not know about `cl.exe`; Cargo can still find the MSVC linker when the Build Tools are installed correctly, but the Developer PowerShell is useful for diagnosing compiler problems.

---

## Step 2 - Check or Install WebView2

Tauri renders Windows apps through Microsoft Edge WebView2. Windows 10 version 1803 and later, and Windows 11, normally already include it.

### Check Whether WebView2 Exists

```powershell
Get-ChildItem `
  -Path "HKLM:\SOFTWARE\WOW6432Node\Microsoft\EdgeUpdate\Clients" `
  -ErrorAction SilentlyContinue |
  Get-ItemProperty |
  Where-Object { $_.name -like "*WebView2*" } |
  Select-Object name, pv
```

If nothing appears and Tauri complains about WebView2, install the Evergreen Runtime from:

[https://developer.microsoft.com/microsoft-edge/webview2/](https://developer.microsoft.com/microsoft-edge/webview2/)

---

## Step 3 - Install Rust with rustup

### Install via winget

```powershell
winget install --id Rustlang.Rustup `
  --accept-source-agreements `
  --accept-package-agreements
```

Close and reopen PowerShell after installation. A full system restart is usually not required. If a model needs to continue in the same PowerShell session, refresh the Cargo PATH immediately:

```powershell
$cargoBin = Join-Path $env:USERPROFILE ".cargo\bin"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$cargoBin*") {
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$cargoBin", "User")
}
if ($env:Path -notlike "*$cargoBin*") {
    $env:Path = "$env:Path;$cargoBin"
}
```

### Or install manually

1. Go to [https://www.rust-lang.org/tools/install](https://www.rust-lang.org/tools/install).
2. Download and run `rustup-init.exe`.
3. Choose the default installation unless you need a custom path.
4. Make sure the default host triple ends with `windows-msvc`.

### Configure the default toolchain

```powershell
rustup default stable-msvc
rustup update
rustup component add rustfmt clippy rust-src
```

### Verify

```powershell
rustc --version
cargo --version
rustup show
rustfmt --version
cargo clippy --version
```

You should see the stable MSVC toolchain selected.

---

## Step 4 - Install Git

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

## Step 5 - Install VS Code and Rust Extensions

```powershell
winget install Microsoft.VisualStudioCode `
  --accept-source-agreements `
  --accept-package-agreements
```

Install recommended extensions:

```powershell
code --install-extension rust-lang.rust-analyzer
code --install-extension vadimcn.vscode-lldb
code --install-extension tamasfe.even-better-toml
code --install-extension tauri-apps.tauri-vscode
```

| Extension | Why It Matters |
|---|---|
| **rust-analyzer** | IntelliSense, type hints, diagnostics, macro expansion |
| **CodeLLDB** | Native Rust debugging |
| **Even Better TOML** | `Cargo.toml`, `rust-toolchain.toml`, and Tauri config support |
| **Tauri** | Tauri commands, schema help, and project integration |

---

## Step 6 - Set Cargo and Agent-Friendly Defaults

Rust projects are easiest for an AI coding agent when the project declares its toolchain, formatting, lint expectations, and verification commands in files that can be read directly from the repository.

### Recommended Files for Every Rust Repository

| File | Purpose |
|---|---|
| `rust-toolchain.toml` | Pins the Rust channel and required components |
| `rustfmt.toml` | Makes formatting deterministic |
| `.cargo/config.toml` | Stores project-level Cargo settings |
| `Cargo.toml` | Defines package/workspace metadata, dependencies, features, and lint policy |
| `README.md` | Explains build, test, run, and release commands |

### `rust-toolchain.toml`

```toml
[toolchain]
channel = "stable"
profile = "default"
components = ["rustfmt", "clippy", "rust-src"]
targets = ["x86_64-pc-windows-msvc"]
```

Use this when you want the same channel everywhere. If a project uses nightly-only features, pin a specific nightly date and explain why in `README.md`.

### `rustfmt.toml`

```toml
edition = "2024"
max_width = 100
newline_style = "Auto"
use_field_init_shorthand = true
use_try_shorthand = true
```

Use `edition = "2021"` if the project or dependencies require it. Do not change an existing project's edition without running the full test suite.

### `.cargo/config.toml`

```toml
[build]
target-dir = "target"

[term]
color = "always"

[alias]
check-all = "check --workspace --all-targets --all-features"
test-all = "test --workspace --all-targets --all-features"
lint = "clippy --workspace --all-targets --all-features -- -D warnings"
fmt-check = "fmt --all -- --check"
```

### Workspace Lints in `Cargo.toml`

For a new workspace, use this pattern:

```toml
[workspace]
resolver = "2"
members = ["crates/*"]

[workspace.package]
edition = "2024"
license = "MIT"
repository = "https://example.com/replace-me"

[workspace.lints.rust]
unsafe_code = "forbid"

[workspace.lints.clippy]
unwrap_used = "warn"
expect_used = "warn"
dbg_macro = "warn"
todo = "warn"
```

For a single package, put lints directly under `[lints.rust]` and `[lints.clippy]`.

---

## Step 7 - Create and Verify a Rust Project

```powershell
mkdir C:\Code
cd C:\Code

cargo new hello-rust
cd hello-rust

cargo run
cargo test
cargo fmt --all
cargo clippy --all-targets --all-features -- -D warnings
cargo doc --no-deps --open
```

### Add Useful Rust CLI Tools

```powershell
cargo install cargo-edit
cargo install cargo-watch
cargo install cargo-nextest --locked
cargo install ripgrep
```

| Tool | Command | Use |
|---|---|---|
| **cargo-edit** | `cargo add anyhow` | Add dependencies without hand-editing TOML |
| **cargo-watch** | `cargo watch -x check -x test` | Re-run checks while coding |
| **cargo-nextest** | `cargo nextest run` | Faster, clearer test runner |
| **ripgrep** | `rg "pattern"` | Fast source search |

---

## Step 8 - Install Node.js for Tauri Frontends

Tauri can use many frontend stacks. For the most common TypeScript/JavaScript Tauri app, install Node.js LTS.

If you already followed the Node.js guide in this folder, skip this step. Otherwise:

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
corepack enable
```

Verify:

```powershell
node --version
npm --version
```

---

## Step 9 - Create a Tauri App

Tauri v2 is the current setup path. The official project generator supports vanilla, React, Vue, Svelte, Solid, Angular, Preact, and Rust frontend options.

### Recommended Starter: TypeScript + Vanilla

This is the simplest baseline for an AI agent because it has fewer framework conventions to infer:

```powershell
cd C:\Code
npm create tauri-app@latest
```

When prompted, choose:

| Prompt | Recommended Choice |
|---|---|
| Project name | `my-tauri-app` |
| Identifier | `com.example.my-tauri-app` |
| Frontend language | `TypeScript / JavaScript` |
| Package manager | `npm` or `pnpm` |
| UI template | `Vanilla` |
| UI flavor | `TypeScript` |

Then run:

```powershell
cd my-tauri-app
npm install
npm run tauri dev
```

Build a production app:

```powershell
npm run tauri build
```

### Cargo-Only Alternative

If you want a Rust-first Tauri project without a Node frontend:

```powershell
cargo install create-tauri-app --locked
cargo create-tauri-app
cd my-tauri-app
cargo install tauri-cli --version "^2.0.0" --locked
cargo tauri dev
```

---

## Step 10 - Understand the Tauri Project Layout

Typical Tauri v2 project:

```text
my-tauri-app/
  package.json
  src/
  src-tauri/
    Cargo.toml
    build.rs
    capabilities/
      default.json
    icons/
    src/
      lib.rs
      main.rs
    tauri.conf.json
```

| Path | What the Agent Should Know |
|---|---|
| `package.json` | Frontend scripts, Tauri npm CLI, frontend dependencies |
| `src/` | Web UI source code |
| `src-tauri/Cargo.toml` | Rust backend dependencies, features, and build settings |
| `src-tauri/src/main.rs` | Desktop app entry point |
| `src-tauri/src/lib.rs` | Tauri setup, commands, plugins, state |
| `src-tauri/tauri.conf.json` | App identity, windows, build commands, bundle settings |
| `src-tauri/capabilities/*.json` | Tauri v2 permission model for frontend access to commands/plugins |

### Tauri Commands Pattern

Expose Rust functions to the frontend with `#[tauri::command]`:

```rust
#[tauri::command]
fn greet(name: &str) -> String {
    format!("Hello, {name}!")
}
```

Register commands in the app builder:

```rust
tauri::Builder::default()
    .invoke_handler(tauri::generate_handler![greet])
    .run(tauri::generate_context!())
    .expect("error while running tauri application");
```

From TypeScript:

```ts
import { invoke } from "@tauri-apps/api/core";

const message = await invoke<string>("greet", { name: "Tauri" });
```

### Tauri Capability Reminder

In Tauri v2, frontend access is permissioned. When adding plugins or commands that need permissions, inspect:

```text
src-tauri/capabilities/default.json
src-tauri/tauri.conf.json
```

Do not blindly broaden capabilities. Grant the smallest permission set that supports the feature.

---

## Agent Programming Contract for Rust and Tauri

When an LLM coding agent works in a Rust or Tauri project, it should follow this contract.

### Before Editing

```powershell
rustc --version
cargo --version
rustup show
cargo metadata --format-version 1 --no-deps
```

For Tauri projects:

```powershell
node --version
npm --version
npm run
```

Read these files before changing behavior:

```text
Cargo.toml
Cargo.lock
rust-toolchain.toml
rustfmt.toml
.cargo/config.toml
README.md
src-tauri/Cargo.toml
src-tauri/tauri.conf.json
src-tauri/capabilities/*.json
package.json
```

### During Coding

- Prefer `cargo add` and `cargo remove` when modifying dependencies.
- Keep `Cargo.lock` committed for applications, including Tauri apps.
- Keep library crates generic and push desktop-specific code into the Tauri crate.
- Use `Result<T, E>` for fallible operations instead of panicking.
- Avoid `unwrap()` and `expect()` in production paths unless there is a strong invariant and a clear message.
- Use `anyhow` for application error plumbing and `thiserror` for library error types.
- Keep long-running work off the UI path; use async commands or background tasks where appropriate.
- Keep Tauri permissions narrow and documented.

### Standard Verification

Run from the Rust workspace root:

```powershell
cargo fmt --all -- --check
cargo clippy --workspace --all-targets --all-features -- -D warnings
cargo test --workspace --all-targets --all-features
cargo doc --workspace --no-deps
```

Run from a Tauri project root:

```powershell
npm install
npm run tauri dev
npm run tauri build
```

If the project has frontend tests or lint scripts:

```powershell
npm run lint
npm test
npm run build
```

### Done Criteria

A Rust/Tauri change is not complete until:

- Formatting passes.
- Clippy passes with the repository's configured strictness.
- Tests pass or the missing tests are clearly explained.
- Tauri dev mode opens successfully for UI changes.
- Tauri build succeeds for packaging or native integration changes.
- New commands/plugins have matching Tauri capability updates.
- README or project docs mention any new setup requirement.

---

## Complete Fresh Install Script

Run this in an **Administrator PowerShell** window on a fresh Windows machine:

```powershell
# ============================================================
# Rust + Tauri Development Environment - Fresh Install Script
# Run in Administrator PowerShell
# ============================================================

Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

# 1. Install Windows native build tools for Rust MSVC and Tauri.
winget install Microsoft.VisualStudio.2022.BuildTools `
  --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive" `
  --accept-source-agreements `
  --accept-package-agreements

# 2. Install Rust.
winget install --id Rustlang.Rustup `
  --accept-source-agreements `
  --accept-package-agreements

# Make rustup/cargo available in this same PowerShell session.
$cargoBin = Join-Path $env:USERPROFILE ".cargo\bin"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$cargoBin*") {
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$cargoBin", "User")
}
if ($env:Path -notlike "*$cargoBin*") {
    $env:Path = "$env:Path;$cargoBin"
}

# 3. Install Git and VS Code.
winget install Git.Git `
  --accept-source-agreements `
  --accept-package-agreements
winget install Microsoft.VisualStudioCode `
  --accept-source-agreements `
  --accept-package-agreements

# 4. Install Node.js LTS through fnm for JavaScript/TypeScript Tauri frontends.
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
corepack enable

# 5. Configure Rust.
rustup default stable-msvc
rustup update
rustup component add rustfmt clippy rust-src

# 6. Install useful Rust tools.
cargo install cargo-edit
cargo install cargo-watch
cargo install cargo-nextest --locked
cargo install ripgrep

# 7. Install VS Code extensions.
code --install-extension rust-lang.rust-analyzer
code --install-extension vadimcn.vscode-lldb
code --install-extension tamasfe.even-better-toml
code --install-extension tauri-apps.tauri-vscode

# 8. Verify.
Write-Host "`n========== Rust + Tauri Installation Summary ==========" -ForegroundColor Cyan
Write-Host "Rust:     $(rustc --version)" -ForegroundColor Green
Write-Host "Cargo:    $(cargo --version)" -ForegroundColor Green
Write-Host "Toolchain:" -ForegroundColor Green
rustup show
Write-Host "Node:     $(node --version)" -ForegroundColor Green
Write-Host "npm:      $(npm --version)" -ForegroundColor Green
Write-Host "Git:      $(git --version)" -ForegroundColor Green
Write-Host "=======================================================" -ForegroundColor Cyan
Write-Host "`nClose and reopen PowerShell before creating your first Tauri app." -ForegroundColor Yellow
```

---

## Quick Reference - Daily Commands

| Task | Command |
|---|---|
| Update Rust | `rustup update` |
| Show active toolchain | `rustup show` |
| Create Rust app | `cargo new my-app` |
| Check compile | `cargo check` |
| Run tests | `cargo test` |
| Format | `cargo fmt --all` |
| Lint | `cargo clippy --all-targets --all-features -- -D warnings` |
| Generate docs | `cargo doc --no-deps --open` |
| Add dependency | `cargo add crate-name` |
| Create Tauri app | `npm create tauri-app@latest` |
| Run Tauri dev | `npm run tauri dev` |
| Build Tauri app | `npm run tauri build` |
| Cargo Tauri dev | `cargo tauri dev` |
| Cargo Tauri build | `cargo tauri build` |

---

## Troubleshooting

### `link.exe` or `cl.exe` errors during `cargo build`

Install or repair Visual Studio Build Tools with the **Desktop development with C++** workload. Restart PowerShell after installation.

```powershell
rustup default stable-msvc
cargo clean
cargo build
```

### `rustc` or `cargo` is not recognized

Close and reopen PowerShell first. A system restart is rarely needed; the common issue is that the current terminal did not reload the user PATH after `rustup` installed.

If it still fails, add Cargo's bin directory to the user PATH and to the current PowerShell session:

```powershell
$cargoBin = Join-Path $env:USERPROFILE ".cargo\bin"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$cargoBin*") {
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$cargoBin", "User")
}
if ($env:Path -notlike "*$cargoBin*") {
    $env:Path = "$env:Path;$cargoBin"
}

rustup --version
rustc --version
cargo --version
```

You can also check whether Cargo's bin directory is visible in the current session:

```powershell
$env:Path -split ';' | Where-Object { $_ -like "*\.cargo\bin*" }
```

The expected user path is:

```text
%USERPROFILE%\.cargo\bin
```

### Tauri says WebView2 is missing

Install the Evergreen WebView2 Runtime from:

[https://developer.microsoft.com/microsoft-edge/webview2/](https://developer.microsoft.com/microsoft-edge/webview2/)

Then restart PowerShell and rerun:

```powershell
npm run tauri dev
```

### `npm run tauri dev` fails before Rust compilation

Check the frontend first:

```powershell
npm install
npm run build
npm run tauri dev
```

If the frontend build fails, fix that before debugging Rust or Tauri.

### Tauri command exists in Rust but fails from TypeScript

Check three things:

1. The Rust function has `#[tauri::command]`.
2. The command is included in `tauri::generate_handler![...]`.
3. The frontend uses the exact command name and argument names expected by Tauri.

### Capability or permission errors in Tauri v2

Inspect:

```text
src-tauri/capabilities/default.json
src-tauri/tauri.conf.json
```

Add the smallest required permission. Avoid broad permissions unless the app genuinely needs them.

---

## Official References

| Topic | Link |
|---|---|
| Rust install | [https://www.rust-lang.org/tools/install](https://www.rust-lang.org/tools/install) |
| Rust book installation | [https://doc.rust-lang.org/book/ch01-01-installation.html](https://doc.rust-lang.org/book/ch01-01-installation.html) |
| Cargo book installation | [https://doc.rust-lang.org/cargo/getting-started/installation.html](https://doc.rust-lang.org/cargo/getting-started/installation.html) |
| Tauri prerequisites | [https://v2.tauri.app/start/prerequisites/](https://v2.tauri.app/start/prerequisites/) |
| Tauri create project | [https://v2.tauri.app/start/create-project/](https://v2.tauri.app/start/create-project/) |
| WebView2 Runtime | [https://developer.microsoft.com/microsoft-edge/webview2/](https://developer.microsoft.com/microsoft-edge/webview2/) |
| Visual C++ Build Tools | [https://visualstudio.microsoft.com/visual-cpp-build-tools/](https://visualstudio.microsoft.com/visual-cpp-build-tools/) |

---

*Last updated: June 2026*
