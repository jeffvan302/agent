# Python 3.12 + AI/ML Development — Clean System Setup Guide

> **Goal:** Starting from a fresh Windows machine, set up Python 3.12 with Miniconda for AI/ML development, including PyTorch install commands for every major hardware platform (NVIDIA GPU, AMD GPU, Intel GPU/NPU, and CPU-only).

---

## Agent-Friendly PowerShell Install

When a model or automation needs to prepare a Windows machine for Python AI/ML work, run this from an **Administrator PowerShell** window. It installs Miniconda silently, creates a Python 3.12 environment, installs editor tooling, and leaves hardware-specific PyTorch installation as an explicit final choice.

```powershell
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

# 1. Install Miniconda silently for the current user.
$minicondaDir = Join-Path $env:USERPROFILE "miniconda3"
$installer = Join-Path $env:TEMP "Miniconda3-latest-Windows-x86_64.exe"
$downloadUrl = "https://repo.anaconda.com/miniconda/Miniconda3-latest-Windows-x86_64.exe"

Invoke-WebRequest -Uri $downloadUrl -OutFile $installer
Start-Process -Wait -FilePath $installer -ArgumentList `
  "/S", `
  "/InstallationType=JustMe", `
  "/RegisterPython=0", `
  "/AddToPath=0", `
  "/D=$minicondaDir"
Remove-Item -LiteralPath $installer -Force

$conda = Join-Path $minicondaDir "Scripts\conda.exe"

# 2. Configure conda.
& $conda init powershell
& $conda config --add channels conda-forge
& $conda config --set channel_priority strict
& $conda config --set auto_activate_base false

# 3. Create the AI environment.
& $conda create -n ai python=3.12 -y

# 4. Install Git and VS Code.
winget install --id Git.Git --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
winget install --id Microsoft.VisualStudioCode --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements

# 5. Install VS Code extensions.
code --install-extension ms-python.python
code --install-extension ms-toolsai.jupyter
code --install-extension ms-python.vscode-pylance

# 6. Install common packages into the environment.
& $conda run -n ai python -m pip install --upgrade pip
& $conda run -n ai python -m pip install `
  numpy pandas matplotlib seaborn scipy scikit-learn `
  jupyterlab notebook ipykernel `
  transformers datasets tokenizers accelerate `
  fastapi uvicorn tqdm rich huggingface_hub

# 7. Register the Jupyter kernel.
& $conda run -n ai python -m ipykernel install --user --name ai --display-name "Python 3.12 (AI)"

# 8. Verify.
& $conda run -n ai python --version
& $conda run -n ai python -m pip --version
git --version
```

After that script completes, install exactly one PyTorch build for the target hardware:

```powershell
conda activate ai

# NVIDIA GPU:
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu128

# Intel GPU/NPU:
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/xpu

# CPU only:
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu
```

Close and reopen PowerShell after the script finishes so `conda activate ai` works through the initialized profile.

---

## Why Python 3.12?

Python 3.12 is the sweet spot for AI/ML development right now:

- **PyTorch** has full support for Python 3.12 (stable since PyTorch 2.4+)
- **TensorFlow** supports Python 3.12 (since TF 2.16+)
- **NumPy, pandas, scikit-learn, Jupyter** all support 3.12
- Most GPU/NPU driver stacks and CUDA toolkits are tested against 3.12
- Python 3.13 is available but many AI packages haven't caught up yet

---

## Step 1 — Install Miniconda (Recommended Path)

Miniconda gives you Python + conda + pip + wheel in one install, plus the ability to create isolated environments for different projects. This is the **recommended** way to set up Python for AI/ML work.

**Download page:** [https://repo.anaconda.com/miniconda/](https://repo.anaconda.com/miniconda/)

### Download the Python 3.12 installer:

Direct link (Windows x86_64):

[https://repo.anaconda.com/miniconda/Miniconda3-py312_26.3.2-2-Windows-x86_64.exe](https://repo.anaconda.com/miniconda/Miniconda3-py312_26.3.2-2-Windows-x86_64.exe)

### Install via GUI:

1. Run the downloaded `Miniconda3-py312_...exe`
2. Accept the license
3. Choose **"Just Me"** (recommended) or **"All Users"**
4. **Important:** Check ✅ **"Add Miniconda3 to my PATH environment variable"** — this makes `python` and `conda` available in every PowerShell window
5. Click Install

### Install via PowerShell (silent):

```powershell
# Download Miniconda Python 3.12 installer
curl.exe -o Miniconda3-py312.exe https://repo.anaconda.com/miniconda/Miniconda3-py312_26.3.2-2-Windows-x86_64.exe

# Run silent install (add to PATH, register as default Python)
Start-Process -Wait -FilePath ".\Miniconda3-py312.exe" -ArgumentList "/S","/InstallationType=JustMe","/AddToPath=1","/RegisterPython=1"

# Clean up
Remove-Item .\Miniconda3-py312.exe
```

### Verify:

Close and reopen PowerShell, then:

```powershell
python --version
# Should show: Python 3.12.x

conda --version
# Should show: conda 26.x.x

pip --version
# Should show: pip 2x.x.x from ...
```

---

## Alternative Path — Plain Python (No Conda)

If you don't want Miniconda and prefer plain Python:

**Download page:** [https://www.python.org/downloads/](https://www.python.org/downloads/)

1. Go to the page above
2. Download Python 3.12.x (the latest 3.12 release)
3. Run the installer
4. ⚠️ **Check "Add Python to PATH"** at the bottom of the first screen
5. Click "Install Now"

### Or via winget:

```powershell
winget install Python.Python.3.12
```

> **Downside:** No `conda` for environment management. You'll use `venv` instead (see Step 3).

---

## Step 2 — Configure Conda and PowerShell

After installing Miniconda, configure it for smooth PowerShell usage:

### 2a. Initialize Conda for PowerShell

```powershell
conda init powershell
```

Close and reopen PowerShell. You should see `(base)` at the start of your prompt.

### 2b. Disable auto-activation of the base environment (optional but recommended)

```powershell
conda config --set auto_activate_base false
```

This prevents Conda from activating the `base` environment every time you open PowerShell. You'll activate environments manually when you need them.

### 2c. Configure Conda to use conda-forge (recommended for AI/ML)

```powershell
conda config --add channels conda-forge
conda config --set channel_priority strict
```

conda-forge has more up-to-date packages and better compatibility for scientific computing.

---

## Step 3 — Create Your AI/ML Environment

Never install packages into the `base` environment. Always create a dedicated environment:

### With Conda (Miniconda path):

```powershell
# Create a Python 3.12 environment called "ai"
conda create -n ai python=3.12 -y

# Activate it
conda activate ai

# Verify
python --version   # Should show Python 3.12.x
```

### With venv (Plain Python path):

```powershell
# Create a virtual environment
python -m venv ai-env

# Activate it
.\ai-env\Scripts\Activate.ps1

# Verify
python --version
```

> **Tip:** You can have multiple environments for different projects. For example, one with PyTorch for NVIDIA, another with PyTorch for CPU-only, etc.

---

## Step 4 — Install PyTorch for Your Hardware

This is the critical step. **The PyTorch install command depends on your hardware.** Pick the section that matches your system.

### How to Check Your GPU

```powershell
# Check for NVIDIA GPU
nvidia-smi

# Check for AMD GPU
# Look in Device Manager under "Display adapters"

# Check for Intel GPU/NPU
# Look in Device Manager under "Display adapters" or "Neural Processing Units"
```

---

### 🔷 Option A: NVIDIA GPU (CUDA) — Most Common for AI

**Best for:** GeForce RTX cards, Quadro, Tesla, any NVIDIA GPU

NVIDIA GPUs are the industry standard for AI/ML training. If you have an NVIDIA GPU, this is what you want.

**Prerequisite:** Install the NVIDIA GPU driver first.

**Driver download:** [https://www.nvidia.com/drivers](https://www.nvidia.com/drivers)

Or via winget:
```powershell
winget install Nvidia.GraphicsDriver
```

Then install PyTorch with CUDA:

```powershell
conda activate ai

# PyTorch with CUDA 12.8 (latest — for RTX 30xx, 40xx, 50xx series)
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu128

# OR: PyTorch with CUDA 12.6 (for older drivers)
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu126

# OR: PyTorch with CUDA 12.4 (for older setups)
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu124
```

**Which CUDA version to pick?**

| Your NVIDIA Driver | CUDA Version | Install Command Suffix |
|---|---|---|
| 550+ (recent) | CUDA 12.8 | `--index-url https://download.pytorch.org/whl/cu128` |
| 535+ | CUDA 12.6 | `--index-url https://download.pytorch.org/whl/cu126` |
| 525+ | CUDA 12.4 | `--index-url https://download.pytorch.org/whl/cu124` |

Check your driver version with `nvidia-smi` — it shows the max CUDA version at the top.

**Verify:**

```powershell
python -c "import torch; print(f'PyTorch: {torch.__version__}'); print(f'CUDA available: {torch.cuda.is_available()}'); print(f'GPU: {torch.cuda.get_device_name(0)}' if torch.cuda.is_available() else 'No GPU detected')"
```

---

### 🔴 Option B: AMD GPU (ROCm) — For AMD Radeon

**Best for:** AMD Radeon RX 7000 series, RX 6000 series, Instinct accelerators

AMD GPU support on Windows is **limited**. ROCm on Windows is still in early stages. For serious AMD GPU training, Linux is recommended.

**On Windows (limited support):**

```powershell
conda activate ai

# AMD ROCm on Windows is experimental — use CPU build for now
# and watch for official Windows ROCm support
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/rocm6.3
```

> ⚠️ **Important:** ROCm on Windows has limited GPU support. Check [https://rocm.docs.amd.com/](https://rocm.docs.amd.com/) for the latest compatibility. If you have an AMD GPU and need full support, consider using WSL2 (Windows Subsystem for Linux) with Ubuntu.

**On Linux (full ROCm support):**

```bash
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/rocm6.3
```

**Verify:**

```powershell
python -c "import torch; print(f'PyTorch: {torch.__version__}'); print(f'ROCm available: {torch.cuda.is_available()}')"
```

---

### 🟢 Option C: Intel GPU / NPU (XPU) — For Intel Arc, Core Ultra

**Best for:** Intel Arc discrete GPUs, Intel Core Ultra processors with NPU, Intel Data Center GPUs

Intel has **upstreamed most optimizations into PyTorch itself**. The Intel Extension for PyTorch (IPEX) is being retired (archived March 2026). For most use cases, stock PyTorch with the XPU device now works.

```powershell
conda activate ai

# Install PyTorch with Intel XPU support
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/xpu

# Install Intel oneAPI runtime (required for XPU)
# Download from: https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit-download.html
# Or install via winget:
winget install Intel.oneAPI.BaseToolkit
```

**For Intel NPU (Neural Processing Unit) on Core Ultra processors:**

```powershell
# Install the OpenVINO runtime for NPU acceleration
pip install openvino

# Install the OpenVINO PyTorch frontend
pip install openvino-dev
```

**Verify:**

```powershell
python -c "import torch; print(f'PyTorch: {torch.__version__}'); print(f'XPU available: {torch.xpu.is_available()}'); print(f'Device: {torch.xpu.get_device_name(0)}' if torch.xpu.is_available() else 'No XPU detected')"
```

---

### ⚪ Option D: CPU Only — No GPU

**Best for:** Laptops without discrete GPUs, servers without GPUs, testing, or when GPU drivers aren't available

```powershell
conda activate ai

# CPU-only PyTorch (smallest download)
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu
```

**Verify:**

```powershell
python -c "import torch; print(f'PyTorch: {torch.__version__}'); print(f'Device: CPU')"
```

---

## Step 5 — Install Essential AI/ML Packages

After installing PyTorch, add the rest of the AI/ML stack:

```powershell
conda activate ai

# Core data science
pip install numpy pandas matplotlib seaborn scipy scikit-learn

# Jupyter notebook (interactive development)
pip install jupyterlab notebook ipykernel

# NLP and transformers
pip install transformers datasets tokenizers accelerate

# Computer vision
pip install opencv-python pillow scikit-image

# Model serving and APIs
pip install fastapi uvicorn

# Progress bars and utilities
pip install tqdm rich

# Hugging Face Hub
pip install huggingface_hub

# Register the environment with Jupyter
python -m ipykernel install --user --name ai --display-name "Python 3.12 (AI)"
```

### Verify the full stack:

```powershell
python -c "
import torch
import numpy as np
import pandas as pd
import transformers
print(f'PyTorch:      {torch.__version__}')
print(f'NumPy:        {np.__version__}')
print(f'Pandas:       {pd.__version__}')
print(f'Transformers: {transformers.__version__}')
print(f'CUDA:         {torch.cuda.is_available()}')
print(f'XPU:          {hasattr(torch, \"xpu\") and torch.xpu.is_available()}')
"
```

---

## Step 6 — Install VS Code and Extensions

```powershell
# Install VS Code (if not already installed)
winget install Microsoft.VisualStudioCode

# Install Python extension
code --install-extension ms-python.python

# Install Jupyter extension (for notebooks in VS Code)
code --install-extension ms-toolsai.jupyter

# Install Pylance (fast IntelliSense for Python)
code --install-extension ms-python.vscode-pylance
```

---

## Step 7 — Install Git

```powershell
winget install Git.Git

# Configure your identity
git config --global user.name "Your Name"
git config --global user.email "your.email@example.com"
```

---

## PyTorch Hardware Quick Reference

| Hardware | Install Command | Device String | Notes |
|---|---|---|---|
| **NVIDIA GPU** | `pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu128` | `"cuda"` | Industry standard. Best support. |
| **AMD GPU** | `pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/rocm6.3` | `"cuda"` (via ROCm) | Windows support is limited. Linux recommended. |
| **Intel GPU/NPU** | `pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/xpu` | `"xpu"` | Requires Intel oneAPI runtime. IPEX is being retired. |
| **CPU only** | `pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu` | `"cpu"` | Smallest download. Works everywhere. |

### How to Use Each Device in Code

```python
import torch

# Auto-detect best available device
if torch.cuda.is_available():
    device = "cuda"          # NVIDIA GPU
elif hasattr(torch, "xpu") and torch.xpu.is_available():
    device = "xpu"           # Intel GPU/NPU
else:
    device = "cpu"           # No GPU

print(f"Using device: {device}")

# Move a tensor to the device
x = torch.randn(3, 3).to(device)
```

---

## Complete Fresh Install Script

Copy and paste this into an **Administrator PowerShell** window on a fresh system:

```powershell
# ============================================================
# Python 3.12 + AI/ML Development — Fresh Install Script
# Run this in an Administrator PowerShell window
# ============================================================

# 1. Allow PowerShell scripts
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

# 2. Download and install Miniconda (Python 3.12)
curl.exe -o Miniconda3-py312.exe https://repo.anaconda.com/miniconda/Miniconda3-py312_26.3.2-2-Windows-x86_64.exe
Start-Process -Wait -FilePath ".\Miniconda3-py312.exe" -ArgumentList "/S","/InstallationType=JustMe","/AddToPath=1","/RegisterPython=1"
Remove-Item .\Miniconda3-py312.exe

# 3. Initialize conda for PowerShell
conda init powershell

# 4. Configure conda
conda config --add channels conda-forge
conda config --set channel_priority strict
conda config --set auto_activate_base false

# 5. Install Git
winget install Git.Git --accept-source-agreements --accept-package-agreements

# 6. Install VS Code
winget install Microsoft.VisualStudioCode --accept-source-agreements --accept-package-agreements

# 7. Install VS Code extensions
code --install-extension ms-python.python
code --install-extension ms-toolsai.jupyter
code --install-extension ms-python.vscode-pylance

# 8. Create AI environment
conda create -n ai python=3.12 -y

Write-Host "`n========== Installation Complete ==========" -ForegroundColor Cyan
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Close and reopen PowerShell" -ForegroundColor White
Write-Host "2. Activate your environment:  conda activate ai" -ForegroundColor White
Write-Host "3. Install PyTorch (pick your hardware from the guide)" -ForegroundColor White
Write-Host "4. Install ML packages:         pip install numpy pandas scikit-learn transformers" -ForegroundColor White
Write-Host "=============================================" -ForegroundColor Cyan
```

**Then install PyTorch for your hardware** (run after reopening PowerShell):

```powershell
conda activate ai

# NVIDIA GPU (most common — pick your CUDA version):
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cu128

# OR: Intel GPU/NPU:
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/xpu

# OR: CPU only:
pip install torch torchvision torchaudio --index-url https://download.pytorch.org/whl/cpu

# Then install the rest:
pip install numpy pandas matplotlib scikit-learn jupyterlab transformers datasets accelerate opencv-python tqdm rich
```

---

## Quick Reference — All Download Links

| Tool | Download Page | winget / Direct Link |
|---|---|---|
| **Miniconda (Python 3.12)** | [https://repo.anaconda.com/miniconda/](https://repo.anaconda.com/miniconda/) | [Direct download (Windows x64)](https://repo.anaconda.com/miniconda/Miniconda3-py312_26.3.2-2-Windows-x86_64.exe) |
| **Python (standalone)** | [https://www.python.org/downloads/](https://www.python.org/downloads/) | `winget install Python.Python.3.12` |
| **NVIDIA GPU Driver** | [https://www.nvidia.com/drivers](https://www.nvidia.com/drivers) | `winget install Nvidia.GraphicsDriver` |
| **Intel oneAPI Toolkit** | [https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit-download.html](https://www.intel.com/content/www/us/en/developer/tools/oneapi/base-toolkit-download.html) | `winget install Intel.oneAPI.BaseToolkit` |
| **Git** | [https://git-scm.com/download/win](https://git-scm.com/download/win) | `winget install Git.Git` |
| **VS Code** | [https://code.visualstudio.com](https://code.visualstudio.com) | `winget install Microsoft.VisualStudioCode` |
| **PyTorch install page** | [https://pytorch.org/get-started/locally/](https://pytorch.org/get-started/locally/) | See hardware-specific commands above |

---

## Conda Environment Cheat Sheet

| Task | Command |
|---|---|
| Create environment | `conda create -n myenv python=3.12 -y` |
| Activate environment | `conda activate myenv` |
| Deactivate environment | `conda deactivate` |
| List all environments | `conda env list` |
| Delete an environment | `conda remove -n myenv --all` |
| Export environment to file | `conda env export > environment.yml` |
| Create from environment file | `conda env create -f environment.yml` |
| Install a package | `conda install numpy` or `pip install numpy` |
| List installed packages | `conda list` or `pip list` |
| Update all packages | `conda update --all` |
| Search for a package | `conda search pandas` |

---

## Troubleshooting

### `conda` is not recognized after install

Close and reopen PowerShell. If it still doesn't work, Miniconda wasn't added to PATH. Either:
- Reinstall with "Add to PATH" checked, or
- Manually add `%USERPROFILE%\miniconda3\Scripts` and `%USERPROFILE%\miniconda3` to your system PATH

### `(base)` appears in every PowerShell prompt

This means auto-activation is on. Disable it:

```powershell
conda config --set auto_activate_base false
```

### PyTorch says `CUDA is not available`

1. Make sure you installed the CUDA version, not the CPU version
2. Check your NVIDIA driver: `nvidia-smi`
3. If the driver is too old, update it from [https://www.nvidia.com/drivers](https://www.nvidia.com/drivers)
4. Reinstall PyTorch with the correct CUDA index URL

### `pip install` fails with permission errors

Make sure you activated your conda environment first:

```powershell
conda activate ai
```

Never use `pip install --user` inside a conda environment — it installs to the wrong location.

### Intel XPU says `device not found`

1. Install the Intel oneAPI Base Toolkit
2. Make sure you installed the XPU version of PyTorch
3. Run `python -c "import torch; print(torch.xpu.is_available())"` to check

### AMD ROCm doesn't work on Windows

ROCm on Windows is still in early stages. For full AMD GPU support:
- Use **WSL2 with Ubuntu** and install ROCm there, or
- Use **Linux natively**, or
- Fall back to **CPU-only** PyTorch on Windows

---

## Python vs Node.js vs C++ vs Android — Setup Comparison

| Aspect | Python (Miniconda) | Node.js | C++ (MSVC) | Android |
|---|---|---|---|---|
| **Download size** | ~95 MB | ~80 MB | ~6 GB | ~8-16 GB |
| **Setup time** | 5 min | 5 min | 15-30 min | 30-60 min |
| **Environment isolation** | ✅ conda environments | ✅ node_modules | ❌ manual | ✅ Gradle |
| **GPU support** | ✅ CUDA/ROCm/XPU | ❌ N/A | ❌ manual | ❌ N/A |
| **Package manager** | conda + pip | npm | vcpkg (optional) | Gradle |
| **Special shell needed?** | No | No | Yes (Developer Cmd) | No |
| **Free for commercial use?** | ✅ Yes | ✅ Yes | ⚠️ Build Tools need license; Community is free for individuals | ✅ Yes |

---

*Last updated: June 2026*
