# Node.js Development — Clean System Setup Guide

> **Goal:** Starting from a fresh Windows machine with nothing installed, get a complete Node.js development environment running from PowerShell. Every tool is free. Every step can be done from the command line.

---

## Agent-Friendly PowerShell Install

When a model or automation needs to prepare a Windows machine for Node.js development, run this from an **Administrator PowerShell** window. It avoids browser downloads and accepts winget prompts up front.

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

# Install command-line dependencies.
winget install --id Schniz.fnm --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
winget install --id Git.Git --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
winget install --id Microsoft.VisualStudioCode --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements

# Configure fnm for PowerShell.
$profileDir = Split-Path $PROFILE
if (-not (Test-Path $profileDir)) { New-Item -ItemType Directory -Path $profileDir -Force | Out-Null }
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

# Install global tools useful to coding agents.
npm install -g typescript ts-node tsx nodemon npm-check-updates

# Install VS Code extensions.
code --install-extension dbaeumer.vscode-eslint
code --install-extension esbenp.prettier-vscode
code --install-extension rangav.vscode-thunder-client

# Verify.
node --version
npm --version
npx --version
tsc --version
git --version
```

Close and reopen PowerShell after the script finishes so the persisted PATH/profile changes are loaded cleanly.

---

## What You'll End Up With

| Tool | What It Does | Installed By |
|---|---|---|
| **fnm** | Node.js version manager — switch between Node versions instantly | winget |
| **Node.js** | JavaScript runtime — runs your code | fnm |
| **npm** | Package manager — installs libraries (bundled with Node.js) | fnm |
| **npx** | Run one-off commands without installing globally (bundled with Node.js) | fnm |
| **TypeScript** | Type-safe JavaScript compiler | npm global |
| **Git** | Version control | winget |
| **VS Code** | Code editor (optional but recommended) | winget |

---

## Step 1 — Install fnm (Fast Node Manager)

fnm is the Node.js version manager recommended on the official Node.js download page. It lets you install and switch between multiple Node.js versions instantly.

**Download page:** [https://github.com/Schniz/fnm](https://github.com/Schniz/fnm)

### Install via winget (easiest):

```powershell
winget install Schniz.fnm
```

### Or install via Chocolatey (if you prefer):

```powershell
choco install fnm
```

### Or install manually:

1. Go to [https://github.com/Schniz/fnm/releases](https://github.com/Schniz/fnm/releases)
2. Download `fnm-windows.zip` from the latest release
3. Extract it and put `fnm.exe` somewhere on your PATH (e.g. `C:\Tools\`)

---

## Step 2 — Configure fnm for PowerShell

After installing fnm, you need to tell PowerShell to activate it every time you open a terminal.

### 2a. Allow PowerShell scripts to run

Open PowerShell as Administrator, then run:

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned
```

Type `Y` and press Enter when prompted.

### 2b. Add fnm to your PowerShell profile

```powershell
notepad $PROFILE
```

If Notepad asks "The file doesn't exist. Do you want to create it?", click **Yes**.

Add this line at the bottom and save:

```powershell
fnm env --use-on-cd --shell powershell | Out-String | Invoke-Expression
```

Close and reopen PowerShell for the change to take effect.

---

## Step 3 — Install Node.js via fnm

Now install the current LTS version of Node.js:

```powershell
# Install the latest LTS version
fnm install --lts

# Tell fnm to use it
fnm use --lts

# Set it as the default
fnm default lts-latest
```

### Verify it worked:

```powershell
node --version
npm --version
fnm list
```

You should see something like:
```
v22.x.x   (LTS)
10.x.x
```

### Installing other versions (anytime later):

```powershell
# See what versions are available
fnm list-remote

# Install a specific version
fnm install 20

# Switch to it
fnm use 20

# Switch back to LTS
fnm use --lts
```

---

## Step 4 — Install Git

**Download page:** [https://git-scm.com/download/win](https://git-scm.com/download/win)

### Install via winget:

```powershell
winget install Git.Git
```

### Or download the installer manually:

1. Go to [https://git-scm.com/download/win](https://git-scm.com/download/win)
2. Download the **64-bit installer** (.exe)
3. Run it — all default options are fine

### Verify:

```powershell
git --version
```

### Configure your identity (required for commits):

```powershell
git config --global user.name "Your Name"
git config --global user.email "your.email@example.com"
```

---

## Step 5 — Install VS Code (Optional but Recommended)

**Download page:** [https://code.visualstudio.com](https://code.visualstudio.com)

### Install via winget:

```powershell
winget install Microsoft.VisualStudioCode
```

### Or download manually:

1. Go to [https://code.visualstudio.com](https://code.visualstudio.com)
2. Download the Windows installer
3. Run it — accept all defaults

### Essential VS Code Extensions for Node.js:

Open VS Code and install these extensions (press `Ctrl+Shift+X` to open the Extensions panel):

| Extension | What It Does | Install Command |
|---|---|---|
| **ESLint** | Catches JavaScript/TypeScript errors | `code --install-extension dbaeumer.vscode-eslint` |
| **Prettier** | Auto-formats your code | `code --install-extension esbenp.prettier-vscode` |
| **TypeScript** | TypeScript language support | Built into VS Code |
| **Thunder Client** | REST API testing (like Postman) | `code --install-extension rangav.vscode-thunder-client` |

Or install them all from PowerShell:

```powershell
code --install-extension dbaeumer.vscode-eslint
code --install-extension esbenp.prettier-vscode
code --install-extension rangav.vscode-thunder-client
```

---

## Step 6 — Install TypeScript Globally

```powershell
npm install -g typescript ts-node
```

### Verify:

```powershell
tsc --version
```

> **Note:** You don't *have* to install TypeScript globally. Most projects include it as a dev dependency (`npm install -D typescript`). But having it globally means you can run `tsc` and `ts-node` anywhere without a project.

---

## Step 7 — Install Useful Global Tools (Optional)

These are nice-to-have tools that work from any PowerShell session:

```powershell
# nodemon — auto-restarts your Node app when files change
npm install -g nodemon

# pm2 — production process manager (keeps apps running, auto-restarts on crash)
npm install -g pm2

# http-server — serve static files instantly for quick testing
npm install -g http-server

# npm-check-updates — check and update package versions
npm install -g npm-check-updates
```

---

## Step 8 — Verify Everything Works

Run this in PowerShell to confirm the full stack:

```powershell
# Should show fnm version
fnm --version

# Should show Node.js LTS version (e.g. v22.x.x)
node --version

# Should show npm version (e.g. 10.x.x)
npm --version

# Should show TypeScript version (e.g. 5.x.x)
tsc --version

# Should show Git version (e.g. 2.x.x)
git --version

# Should show VS Code version
code --version
```

If all six commands return version numbers, your environment is ready.

---

## Step 9 — Create Your First Project

Now that everything is installed, here's how to create and run a project from PowerShell:

### Simple JavaScript Project

```powershell
# Create project folder
mkdir MyFirstApp
cd MyFirstApp

# Initialize package.json
npm init -y

# Install Express (web framework)
npm install express

# Create a simple server
@'
const express = require("express");
const app = express();
const port = 3000;

app.get("/", (req, res) => {
  res.send("Hello from Node.js!");
});

app.listen(port, () => {
  console.log(`Server running at http://localhost:${port}`);
});
'@ | Out-File -Encoding utf8 -FilePath "index.js"

# Run it
node index.js
```

Open your browser to **http://localhost:3000** — you should see "Hello from Node.js!"

Press `Ctrl+C` in PowerShell to stop the server.

### TypeScript Project

```powershell
# Create project folder
mkdir MyTsApp
cd MyTsApp

# Initialize
npm init -y

# Install TypeScript and types
npm install -D typescript @types/node tsx

# Create TypeScript config
npx tsc --init

# Create a simple server
@'
import express from "express";
const app = express();
const port = 3000;

app.get("/", (req, res) => {
  res.send("Hello from TypeScript!");
});

app.listen(port, () => {
  console.log(`Server running at http://localhost:${port}`);
});
'@ | Out-File -Encoding utf8 -FilePath "src/index.ts"

# Install Express types
npm install express
npm install -D @types/express

# Run it with tsx (TypeScript runner — no compile step needed)
npx tsx src/index.ts
```

### React Project

```powershell
npx create-react-app my-react-app
cd my-react-app
npm start
```

### Next.js Project

```powershell
npx create-next-app@latest my-nextjs-app
cd my-nextjs-app
npm run dev
```

---

## Quick Reference — All Download Links

| Tool | Download Page | winget Command |
|---|---|---|
| **fnm** | [https://github.com/Schniz/fnm](https://github.com/Schniz/fnm) | `winget install Schniz.fnm` |
| **Node.js** | Installed via fnm (see Step 3) | `fnm install --lts` |
| **Git** | [https://git-scm.com/download/win](https://git-scm.com/download/win) | `winget install Git.Git` |
| **VS Code** | [https://code.visualstudio.com](https://code.visualstudio.com) | `winget install Microsoft.VisualStudioCode` |
| **TypeScript** | Installed via npm | `npm install -g typescript ts-node` |
| **nodemon** | Installed via npm | `npm install -g nodemon` |
| **pm2** | Installed via npm | `npm install -g pm2` |

---

## Complete Fresh Install Script

Copy and paste this entire block into an **Administrator PowerShell** window on a fresh system. It installs everything in order:

```powershell
# ============================================================
# Node.js Development Environment — Fresh Install Script
# Run this in an Administrator PowerShell window
# ============================================================

# 1. Allow PowerShell scripts to run
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

# 2. Install fnm (Node version manager)
winget install Schniz.fnm --accept-source-agreements --accept-package-agreements

# 3. Configure fnm in PowerShell profile
$profileDir = Split-Path $PROFILE
if (-not (Test-Path $profileDir)) { New-Item -ItemType Directory -Path $profileDir -Force }
$fnmLine = 'fnm env --use-on-cd --shell powershell | Out-String | Invoke-Expression'
if (Test-Path $PROFILE) {
    $content = Get-Content $PROFILE -Raw
    if ($content -notmatch "fnm env") {
        Add-Content -Path $PROFILE -Value "`n$fnmLine"
    }
} else {
    Set-Content -Path $PROFILE -Value $fnmLine
}

# 4. Activate fnm in this session
fnm env --use-on-cd --shell powershell | Out-String | Invoke-Expression

# 5. Install Node.js LTS
fnm install --lts
fnm use --lts
fnm default lts-latest

# 6. Install Git
winget install Git.Git --accept-source-agreements --accept-package-agreements

# 7. Install VS Code
winget install Microsoft.VisualStudioCode --accept-source-agreements --accept-package-agreements

# 8. Install global npm tools
npm install -g typescript ts-node nodemon pm2

# 9. Verify everything
Write-Host "`n========== Installation Summary ==========" -ForegroundColor Cyan
Write-Host "fnm:       $(fnm --version)" -ForegroundColor Green
Write-Host "Node.js:   $(node --version)" -ForegroundColor Green
Write-Host "npm:       $(npm --version)" -ForegroundColor Green
Write-Host "TypeScript:$(tsc --version)" -ForegroundColor Green
Write-Host "Git:       $(git --version)" -ForegroundColor Green
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "`nAll done! Close and reopen PowerShell, then run:" -ForegroundColor Yellow
Write-Host "  node --version" -ForegroundColor White
Write-Host "  npm --version" -ForegroundColor White
Write-Host "  tsc --version" -ForegroundColor White
Write-Host "`nIf those return version numbers, you're ready to develop!" -ForegroundColor Yellow
```

---

## Troubleshooting

### `fnm` is not recognized after installing

Close and reopen PowerShell. fnm modifies your PATH during install, but existing PowerShell windows won't see the change until they're restarted.

### `node` is not recognized after `fnm install`

You need the PowerShell profile hook. Make sure Step 2b was completed:

```powershell
notepad $PROFILE
```

Verify this line exists:

```powershell
fnm env --use-on-cd --shell powershell | Out-String | Invoke-Expression
```

If not, add it, save, and restart PowerShell.

### `winget` is not recognized

winget comes built into Windows 10 (version 1709+) and Windows 11. If it's missing:

1. Open the **Microsoft Store** app
2. Search for **App Installer**
3. Update it
4. Restart PowerShell

### npm install fails with `node-gyp` errors

Some packages need native C++ compilation. Install the build tools:

```powershell
npm install -g windows-build-tools
# Or install Visual Studio Build Tools manually from:
# https://visualstudio.microsoft.com/visual-cpp-build-tools/
```

Then tell npm where to find them:

```powershell
npm config set msvs_version 2022
```

### TypeScript `tsc` is not recognized

Make sure the npm global bin directory is on your PATH:

```powershell
npm config get prefix
```

The output should be something like `C:\Users\YourName\AppData\Roaming\npm`. Make sure that directory is in your system PATH.

---

## What About Bun?

Bun is an alternative runtime to Node.js — it's faster at installing packages and running scripts. You already have it on your current system (v1.3.12). On a fresh system:

```powershell
winget install Oven-sh.Bun
```

Or install via PowerShell:

```powershell
irm bun.sh/install.ps1 | iex
```

Bun can run most Node.js projects and is significantly faster at `npm install`. However, it's not a 100% drop-in replacement for Node.js — some native modules don't work with it. Use it as a speed boost for package installation, but keep Node.js as your primary runtime.

---

*Last updated: June 2026*
