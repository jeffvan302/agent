# Android Development - Clean System Setup Guide

> **Goal:** Set up a complete Android development environment on Windows that can be driven from PowerShell by a human or an AI coding agent. Android Studio is still the easiest full IDE path, but this guide also includes a command-line-only SDK path for automation.

---

## What You'll End Up With

| Tool | What It Does | Installed By |
|---|---|---|
| **Android Studio** | Full IDE, emulator manager, SDK Manager, bundled JetBrains Runtime | Browser or winget |
| **Android SDK Command-Line Tools** | Provides `sdkmanager` and `avdmanager` | Android Studio or direct zip download |
| **Android SDK Platform Tools** | Provides `adb` and `fastboot` | `sdkmanager` |
| **Android Emulator** | Runs Android virtual devices | `sdkmanager` |
| **Android Platform + Build Tools** | Compiles apps for a target Android API | `sdkmanager` |
| **JDK 21** | Java runtime/compiler for Gradle and Android builds | Android Studio bundled JBR or winget |
| **Gradle Wrapper** | Project-local build runner | Generated or committed by each Android project |

---

## Choose an Install Path

| Path | Best For | Command-Line Friendly |
|---|---|---|
| **A. Android Studio via browser** | Humans who want the full IDE and setup wizard | Partially |
| **B. Android Studio via winget** | Agents or admins that can install the IDE silently | Yes |
| **C. Command-line SDK only** | Headless automation, CI, or agents that only need build/test tools | Yes |

Use Path B or C when the environment is being prepared by a model from PowerShell.

---

## Path A - Install Android Studio Manually

Official download page:

[https://developer.android.com/studio](https://developer.android.com/studio)

1. Open the page above in a browser.
2. Accept the terms and download the Windows installer.
3. Run the installer and follow the Setup Wizard.
4. Let the wizard download the default SDK components.
5. Open **Tools > SDK Manager > SDK Tools**.
6. Enable **Android SDK Command-line Tools (latest)**.
7. Click **Apply**.

This path gives you Android Studio, the bundled JDK, the SDK Manager UI, and the emulator UI.

---

## Path B - Install Android Studio from PowerShell

Run this in an Administrator PowerShell window:

```powershell
winget install --id Google.AndroidStudio --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
```

Android Studio installs to:

```text
C:\Program Files\Android\Android Studio
```

The bundled JDK is normally:

```text
C:\Program Files\Android\Android Studio\jbr
```

After this, either run Android Studio once for the setup wizard, or use Path C below to install SDK packages from PowerShell.

---

## Path C - Command-Line SDK Only

This path does not install the Android Studio IDE. It installs a JDK, downloads Google's Android SDK command-line tools, installs the SDK packages, and configures user environment variables.

### Step C1 - Install JDK 21

Android Studio includes a bundled JDK, but the SDK-only path needs one installed separately:

```powershell
winget install --id EclipseAdoptium.Temurin.21.JDK --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
```

Close and reopen PowerShell, then verify:

```powershell
java -version
javac -version
```

### Step C2 - Download and Install Android Command-Line Tools

The command-line tools are listed on the official Android Studio download page under **Command line tools only**. The direct URL changes over time, so if this URL fails, copy the latest Windows command-line tools URL from:

[https://developer.android.com/studio#command-tools](https://developer.android.com/studio#command-tools)

```powershell
$androidHome = Join-Path $env:LOCALAPPDATA "Android\Sdk"
$cmdlineRoot = Join-Path $androidHome "cmdline-tools"
$latestTools = Join-Path $cmdlineRoot "latest"
$downloadUrl = "https://dl.google.com/android/repository/commandlinetools-win-14742923_latest.zip"
$zipPath = Join-Path $env:TEMP "android-commandlinetools.zip"
$extractRoot = Join-Path $env:TEMP "android-commandlinetools"

New-Item -ItemType Directory -Force -Path $cmdlineRoot | Out-Null
Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath

if (Test-Path $extractRoot) {
    Remove-Item -LiteralPath $extractRoot -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
Expand-Archive -LiteralPath $zipPath -DestinationPath $extractRoot -Force

if (Test-Path $latestTools) {
    $backupPath = "$latestTools.backup.$(Get-Date -Format yyyyMMddHHmmss)"
    Rename-Item -LiteralPath $latestTools -NewName (Split-Path $backupPath -Leaf)
}

Move-Item -LiteralPath (Join-Path $extractRoot "cmdline-tools") -Destination $latestTools
Remove-Item -LiteralPath $zipPath -Force
```

### Step C3 - Set Android Environment Variables

```powershell
$androidHome = Join-Path $env:LOCALAPPDATA "Android\Sdk"
$jdkHome = "${env:ProgramFiles}\Eclipse Adoptium\jdk-21"

if (-not (Test-Path $jdkHome)) {
    $jdkHome = Get-ChildItem "${env:ProgramFiles}\Eclipse Adoptium" -Directory -Filter "jdk-21*" |
        Sort-Object Name -Descending |
        Select-Object -First 1 -ExpandProperty FullName
}

[Environment]::SetEnvironmentVariable("ANDROID_HOME", $androidHome, "User")
[Environment]::SetEnvironmentVariable("ANDROID_SDK_ROOT", $androidHome, "User")
if ($jdkHome) {
    [Environment]::SetEnvironmentVariable("JAVA_HOME", $jdkHome, "User")
}

$pathsToAdd = @(
    "$androidHome\platform-tools",
    "$androidHome\emulator",
    "$androidHome\cmdline-tools\latest\bin"
)
if ($jdkHome) {
    $pathsToAdd += "$jdkHome\bin"
}

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
foreach ($path in $pathsToAdd) {
    if ($userPath -notlike "*$path*") {
        $userPath = "$userPath;$path"
    }
}
[Environment]::SetEnvironmentVariable("Path", $userPath, "User")

$env:ANDROID_HOME = $androidHome
$env:ANDROID_SDK_ROOT = $androidHome
if ($jdkHome) { $env:JAVA_HOME = $jdkHome }
$env:Path = "$env:Path;$($pathsToAdd -join ';')"
```

### Step C4 - Install SDK Packages

This installs Android 16 / API 36 packages, build tools, platform tools, emulator, and a Google APIs x86_64 emulator image.

```powershell
$sdkmanager = "$env:ANDROID_HOME\cmdline-tools\latest\bin\sdkmanager.bat"

& $sdkmanager --sdk_root="$env:ANDROID_HOME" `
  "cmdline-tools;latest" `
  "platform-tools" `
  "emulator" `
  "platforms;android-36" `
  "build-tools;36.0.0" `
  "system-images;android-36;google_apis;x86_64"
```

Accept Android SDK licenses:

```powershell
for ($i = 0; $i -lt 20; $i++) { "y" } | & $sdkmanager --sdk_root="$env:ANDROID_HOME" --licenses
```

If a package name is unavailable, list current packages and choose the nearest stable version:

```powershell
& $sdkmanager --sdk_root="$env:ANDROID_HOME" --list
```

---

## Create a Pixel Emulator from PowerShell

Create a Pixel 8 emulator using the installed Android 16 system image:

```powershell
avdmanager create avd `
  --name "Pixel_8_API_36" `
  --device "pixel_8" `
  --package "system-images;android-36;google_apis;x86_64" `
  --tag "google_apis"
```

When asked whether to create a custom hardware profile, type `no`.

Start the emulator:

```powershell
emulator -avd Pixel_8_API_36
```

Or run it without a visible emulator window:

```powershell
Start-Process emulator -ArgumentList "-avd","Pixel_8_API_36","-no-window" -NoNewWindow
```

Verify:

```powershell
adb devices
```

You should see an `emulator-5554` device after the emulator finishes booting.

---

## Agent-Friendly Fresh Install Script

Run this in an Administrator PowerShell window when a model needs to prepare Android build tooling without using the Android Studio UI:

```powershell
# ============================================================
# Android SDK Command-Line Environment - Fresh Install Script
# Run in Administrator PowerShell
# ============================================================

Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

# 1. Install JDK 21.
winget install --id EclipseAdoptium.Temurin.21.JDK --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements

# 2. Define SDK paths.
$androidHome = Join-Path $env:LOCALAPPDATA "Android\Sdk"
$cmdlineRoot = Join-Path $androidHome "cmdline-tools"
$latestTools = Join-Path $cmdlineRoot "latest"
$downloadUrl = "https://dl.google.com/android/repository/commandlinetools-win-14742923_latest.zip"
$zipPath = Join-Path $env:TEMP "android-commandlinetools.zip"
$extractRoot = Join-Path $env:TEMP "android-commandlinetools"

# 3. Download and install Android command-line tools.
New-Item -ItemType Directory -Force -Path $cmdlineRoot | Out-Null
Invoke-WebRequest -Uri $downloadUrl -OutFile $zipPath
if (Test-Path $extractRoot) { Remove-Item -LiteralPath $extractRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
Expand-Archive -LiteralPath $zipPath -DestinationPath $extractRoot -Force
if (Test-Path $latestTools) {
    Rename-Item -LiteralPath $latestTools -NewName "latest.backup.$(Get-Date -Format yyyyMMddHHmmss)"
}
Move-Item -LiteralPath (Join-Path $extractRoot "cmdline-tools") -Destination $latestTools
Remove-Item -LiteralPath $zipPath -Force

# 4. Configure environment variables.
$jdkHome = Get-ChildItem "${env:ProgramFiles}\Eclipse Adoptium" -Directory -Filter "jdk-21*" |
    Sort-Object Name -Descending |
    Select-Object -First 1 -ExpandProperty FullName

[Environment]::SetEnvironmentVariable("ANDROID_HOME", $androidHome, "User")
[Environment]::SetEnvironmentVariable("ANDROID_SDK_ROOT", $androidHome, "User")
[Environment]::SetEnvironmentVariable("JAVA_HOME", $jdkHome, "User")

$pathsToAdd = @(
    "$androidHome\platform-tools",
    "$androidHome\emulator",
    "$androidHome\cmdline-tools\latest\bin",
    "$jdkHome\bin"
)
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
foreach ($path in $pathsToAdd) {
    if ($userPath -notlike "*$path*") { $userPath = "$userPath;$path" }
}
[Environment]::SetEnvironmentVariable("Path", $userPath, "User")

$env:ANDROID_HOME = $androidHome
$env:ANDROID_SDK_ROOT = $androidHome
$env:JAVA_HOME = $jdkHome
$env:Path = "$env:Path;$($pathsToAdd -join ';')"

# 5. Install SDK packages.
$sdkmanager = "$androidHome\cmdline-tools\latest\bin\sdkmanager.bat"
& $sdkmanager --sdk_root="$androidHome" `
  "cmdline-tools;latest" `
  "platform-tools" `
  "emulator" `
  "platforms;android-36" `
  "build-tools;36.0.0" `
  "system-images;android-36;google_apis;x86_64"

# 6. Accept licenses.
for ($i = 0; $i -lt 20; $i++) { "y" } | & $sdkmanager --sdk_root="$androidHome" --licenses

# 7. Verify.
Write-Host "`n========== Android SDK Installation Summary ==========" -ForegroundColor Cyan
Write-Host "JAVA_HOME:        $env:JAVA_HOME" -ForegroundColor Green
Write-Host "ANDROID_HOME:     $env:ANDROID_HOME" -ForegroundColor Green
Write-Host "Java:" -ForegroundColor Green
java -version
Write-Host "sdkmanager:" -ForegroundColor Green
& $sdkmanager --version
Write-Host "adb:" -ForegroundColor Green
adb version
Write-Host "======================================================" -ForegroundColor Cyan
Write-Host "`nClose and reopen PowerShell before creating an emulator." -ForegroundColor Yellow
```

---

## Build and Run an Android App from PowerShell

Once the environment is set up, a model can generate or modify the Android project and run these commands from the project root.

### Build the APK

```powershell
cd "C:\path\to\your\project"
.\gradlew.bat assembleDebug
```

The APK is usually written to:

```text
app\build\outputs\apk\debug\app-debug.apk
```

### Install on Emulator or Physical Device

```powershell
adb devices
adb install app\build\outputs\apk\debug\app-debug.apk
```

### Install on a Physical Phone

1. Enable **Developer Options** and **USB Debugging** on the phone.
2. Plug it in via USB.
3. Run `adb devices`.
4. Approve the computer on the phone if prompted.
5. Run `adb install app\build\outputs\apk\debug\app-debug.apk`.

---

## Agent Programming Contract for Android

Before editing:

```powershell
java -version
sdkmanager --version
adb version
.\gradlew.bat --version
.\gradlew.bat projects
```

Read whichever of these files exist:

```text
settings.gradle
settings.gradle.kts
build.gradle
build.gradle.kts
gradle.properties
gradle/wrapper/gradle-wrapper.properties
app/build.gradle
app/build.gradle.kts
app/src/main/AndroidManifest.xml
app/src/main/java/
app/src/main/kotlin/
app/src/main/res/
```

Standard verification:

```powershell
.\gradlew.bat clean
.\gradlew.bat assembleDebug
.\gradlew.bat testDebugUnitTest
.\gradlew.bat lintDebug
```

For connected-device or emulator tests:

```powershell
adb devices
.\gradlew.bat connectedDebugAndroidTest
```

Done criteria:

- Gradle sync/build files are valid.
- `assembleDebug` passes.
- Unit tests pass when present.
- Android lint passes or documented warnings remain.
- Emulator/device install succeeds for app changes.
- New SDK/platform requirements are documented.

---

## Quick Reference - Key Commands

| Task | Command |
|---|---|
| Install Android Studio | `winget install --id Google.AndroidStudio --exact --silent` |
| Install JDK 21 | `winget install --id EclipseAdoptium.Temurin.21.JDK --exact --silent` |
| List SDK packages | `sdkmanager --list` |
| Install platform tools | `sdkmanager "platform-tools"` |
| Install Android 16 platform | `sdkmanager "platforms;android-36"` |
| Install build tools | `sdkmanager "build-tools;36.0.0"` |
| Install emulator | `sdkmanager "emulator"` |
| Install system image | `sdkmanager "system-images;android-36;google_apis;x86_64"` |
| Accept SDK licenses | `sdkmanager --licenses` |
| List devices | `adb devices` |
| List AVD device profiles | `avdmanager list device` |
| List existing AVDs | `avdmanager list avd` |
| Create AVD | `avdmanager create avd --name "Pixel_8_API_36" --device "pixel_8" --package "system-images;android-36;google_apis;x86_64" --tag "google_apis"` |
| Start emulator | `emulator -avd Pixel_8_API_36` |
| Build debug APK | `.\gradlew.bat assembleDebug` |
| Install APK | `adb install app\build\outputs\apk\debug\app-debug.apk` |
| View logs | `adb logcat` |

---

## What Android Studio Gives You

| Component | Typical Location |
|---|---|
| Android Studio IDE | `C:\Program Files\Android\Android Studio` |
| Bundled JDK/JBR | `C:\Program Files\Android\Android Studio\jbr` |
| Android SDK | `%LOCALAPPDATA%\Android\Sdk` |
| Command-line tools | `%LOCALAPPDATA%\Android\Sdk\cmdline-tools\latest` |
| Platform tools | `%LOCALAPPDATA%\Android\Sdk\platform-tools` |
| Emulator | `%LOCALAPPDATA%\Android\Sdk\emulator` |
| Build tools | `%LOCALAPPDATA%\Android\Sdk\build-tools` |
| Gradle wrapper cache | `%USERPROFILE%\.gradle\wrapper\dists` |

---

## Troubleshooting

### `sdkmanager` is not recognized

Close and reopen PowerShell. Confirm this directory is on PATH:

```text
%ANDROID_HOME%\cmdline-tools\latest\bin
```

### `java -version` shows the wrong version

Check `JAVA_HOME`:

```powershell
$env:JAVA_HOME
java -version
```

For Android Studio, `JAVA_HOME` can point to:

```text
C:\Program Files\Android\Android Studio\jbr
```

For SDK-only installs, point it to the installed JDK 21 directory.

### Android command-line tools URL fails

The command-line tools URL changes over time. Open:

[https://developer.android.com/studio#command-tools](https://developer.android.com/studio#command-tools)

Copy the latest Windows `commandlinetools-win-..._latest.zip` link into `$downloadUrl`.

### Emulator will not start

- Enable virtualization in BIOS/UEFI.
- Enable **Windows Hypervisor Platform** in Windows Features.
- Try `emulator -avd Pixel_8_API_36 -verbose`.

### `adb devices` shows no devices

- Wait for the emulator to finish booting.
- For USB devices, enable USB Debugging and approve the computer prompt.
- Restart ADB:

```powershell
adb kill-server
adb start-server
adb devices
```

### Gradle build fails with JDK errors

Use JDK 17 or newer. JDK 21 works well for modern Android Gradle Plugin versions:

```powershell
java -version
$env:JAVA_HOME
```

---

## Official References

| Topic | Link |
|---|---|
| Android Studio downloads | [https://developer.android.com/studio](https://developer.android.com/studio) |
| Command-line tools only | [https://developer.android.com/studio#command-tools](https://developer.android.com/studio#command-tools) |
| sdkmanager | [https://developer.android.com/tools/sdkmanager](https://developer.android.com/tools/sdkmanager) |
| avdmanager | [https://developer.android.com/tools/avdmanager](https://developer.android.com/tools/avdmanager) |
| Android 16 SDK setup | [https://developer.android.com/about/versions/16/setup-sdk](https://developer.android.com/about/versions/16/setup-sdk) |
| Platform tools release notes | [https://developer.android.com/studio/releases/platform-tools](https://developer.android.com/studio/releases/platform-tools) |

---

*Last updated: June 2026*
