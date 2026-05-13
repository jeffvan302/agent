<#
.SYNOPSIS
    Downloads and unpacks a prebuilt Win64 OpenSSL 4.x distribution into
    third_party\openssl\ so that build.bat can automatically enable HTTPS/TLS.

.DESCRIPTION
    Downloads the full (non-Light) Win64 installer from Shining Light
    Productions (https://slproweb.com/products/Win32OpenSSL.html).
    The full installer includes BOTH static libs (libssl_static.lib /
    libcrypto_static.lib) AND the dynamic DLLs.

    Static libs are preferred by build.bat so the final agent.exe is
    fully self-contained -- no separate DLLs need to ship with the app.

    Resulting layout expected by build.bat:

        third_party\openssl\
          include\openssl\*.h
          lib\VC\x64\MD\libssl_static.lib      (release /MD static)
          lib\VC\x64\MD\libcrypto_static.lib
          lib\VC\x64\MDd\libssl_static.lib     (debug  /MDd static)
          lib\VC\x64\MDd\libcrypto_static.lib
          lib\libssl.lib                       (dynamic import fallback)
          lib\libcrypto.lib
          bin\libssl-4-x64.dll                 (dynamic fallback DLLs)
          bin\libcrypto-4-x64.dll

.NOTES
    Requires PowerShell 5.1+ and an internet connection.
    Run from the repository root:
        powershell -ExecutionPolicy Bypass scripts\download_openssl.ps1
#>

[CmdletBinding()]
param(
    # OpenSSL version to download (x.y.z)
    [string]$Version = "4.0.0",

    # Override the target directory (default: third_party\openssl relative to repo root)
    [string]$TargetDir = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ---------- Resolve paths -------------------------------------------------------
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$RepoRoot  = Split-Path -Parent $ScriptDir

if ($TargetDir -eq "") {
    $TargetDir = Join-Path $RepoRoot 'third_party\openssl'
}

Write-Host "OpenSSL target directory : $TargetDir"
Write-Host "OpenSSL version          : $Version"

# ---------- Build download URL --------------------------------------------------
# Shining Light Productions URL pattern -- FULL (non-Light) installer:
#   Win64OpenSSL-4_0_0.exe
$VerUnd   = $Version -replace "\.",  "_"
$Filename = "Win64OpenSSL-${VerUnd}.exe"
$TempExe  = Join-Path $env:TEMP $Filename
$InstDir  = Join-Path $env:TEMP "openssl_install_${VerUnd}"

# Try primary .exe URL, then MSI (slproweb occasionally renames between releases).
$CandidateUrls = @(
    "https://slproweb.com/download/$Filename",
    "https://slproweb.com/download/Win64OpenSSL-${VerUnd}.msi"
)

# ---------- Download installer --------------------------------------------------
if (-not (Test-Path $TempExe)) {
    $downloaded = $false
    foreach ($Url in $CandidateUrls) {
        Write-Host "Trying $Url ..."
        try {
            Invoke-WebRequest -Uri $Url -OutFile $TempExe -UseBasicParsing
            $downloaded = $true
            Write-Host "  Downloaded OK."
            break
        } catch {
            Write-Host "  Failed: $_"
        }
    }
    if (-not $downloaded) {
        Write-Error (
            "All download attempts failed.`n" +
            "Please download $Filename manually from " +
            "https://slproweb.com/products/Win32OpenSSL.html " +
            "and place it at $TempExe"
        )
        exit 1
    }
} else {
    Write-Host "Using cached installer : $TempExe"
}

# ---------- Run installer silently ----------------------------------------------
Write-Host "Installing OpenSSL to $InstDir ..."
$null = New-Item -ItemType Directory -Force -Path $InstDir

# Shining Light's WiX package launches this same Inno installer with the /MSI
# token below. Newer OpenSSL 4 installers exit with code 2 without it, even
# when the normal Inno silent switches are otherwise valid.
$msiBridgeToken = '/MSI=8BA62D91-E3D5-4E94-AEDB-7BB3BA1B578E'
$proc = Start-Process -FilePath $TempExe `
    -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
        '/RESTARTEXITCODE=0', $msiBridgeToken, "/DIR=$InstDir" `
    -Wait -PassThru
if ($proc.ExitCode -ne 0) {
    Write-Error "Installer exited with code $($proc.ExitCode)"
    exit 1
}

# ---------- Helper: copy file with existence check ------------------------------
function Copy-Safe([string]$Src, [string]$Dst) {
    if (Test-Path $Src) {
        $null = New-Item -ItemType Directory -Force -Path (Split-Path $Dst)
        Copy-Item $Src $Dst -Force
        return $true
    }
    return $false
}

# ---------- Copy headers --------------------------------------------------------
Write-Host "Copying headers ..."
$IncSrc  = Join-Path $InstDir  'include\openssl'
$IncDest = Join-Path $TargetDir 'include\openssl'
if (Test-Path $IncSrc) {
    $null = New-Item -ItemType Directory -Force -Path $IncDest
    Copy-Item "$IncSrc\*" $IncDest -Recurse -Force
} else {
    Write-Warning "Include directory not found at $IncSrc"
}

# ---------- Copy static libs (primary goal -- self-contained exe) ---------------
Write-Host "Copying static libraries ..."

# Shining Light full installer layout (OpenSSL 3.x/4.x):
#   lib\VC\x64\MD\   -- release /MD
#   lib\VC\x64\MDd\  -- debug   /MDd
foreach ($rt in @("MD", "MDd")) {
    $srcDir = Join-Path $InstDir   "lib\VC\x64\$rt"
    $dstDir = Join-Path $TargetDir "lib\VC\x64\$rt"
    foreach ($lib in @("libssl_static.lib", "libcrypto_static.lib",
                        "libssl.lib", "libcrypto.lib")) {
        $s = Join-Path $srcDir $lib
        $d = Join-Path $dstDir $lib
        if (-not (Copy-Safe $s $d)) {
            Write-Verbose "  Not found (may be normal): $s"
        }
    }
}

# Some builds also place libs flat in lib\
foreach ($lib in @("libssl_static.lib", "libcrypto_static.lib",
                    "libssl.lib", "libcrypto.lib")) {
    $s = Join-Path $InstDir   "lib\$lib"
    $d = Join-Path $TargetDir "lib\$lib"
    Copy-Safe $s $d | Out-Null
}

# ---------- Copy runtime DLLs (dynamic fallback) --------------------------------
Write-Host "Copying runtime DLLs ..."
$BinSrc  = Join-Path $InstDir  'bin'
$BinDest = Join-Path $TargetDir 'bin'
if (Test-Path $BinSrc) {
    $null = New-Item -ItemType Directory -Force -Path $BinDest
    foreach ($dll in (Get-ChildItem $BinSrc -Filter "*.dll")) {
        Copy-Item $dll.FullName (Join-Path $BinDest $dll.Name) -Force
    }
}

# ---------- Verify the most important outputs -----------------------------------
Write-Host ""
Write-Host "Verification:"

$checks = @(
    (Join-Path $TargetDir 'include\openssl\ssl.h'),
    (Join-Path $TargetDir 'lib\VC\x64\MD\libssl_static.lib'),
    (Join-Path $TargetDir 'lib\VC\x64\MD\libcrypto_static.lib'),
    (Join-Path $TargetDir 'lib\VC\x64\MDd\libssl_static.lib'),
    (Join-Path $TargetDir 'lib\VC\x64\MDd\libcrypto_static.lib')
)
$ok = $true
foreach ($f in $checks) {
    if (Test-Path $f) {
        Write-Host "  OK      $f"
    } else {
        Write-Warning "  MISSING $f"
        $ok = $false
    }
}

Write-Host ""
if ($ok) {
    Write-Host "OpenSSL $Version ready in third_party\openssl"
    Write-Host "Static linking enabled -- agent.exe will be fully self-contained."
    Write-Host ""
    Write-Host "Next step: rebuild the project with build.bat"
} else {
    Write-Warning "Some static libraries are missing."
    Write-Warning "Inspect $InstDir for the actual layout and copy manually if needed."
    Write-Warning "Dynamic fallback may still work if lib\libssl.lib is present."
}
