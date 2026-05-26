# Android Development Quick Start Guide

> **Goal:** Set up a complete Android development environment that can be driven entirely from PowerShell — no need to open Android Studio's GUI after initial setup.

---

## 1. Download and Install Android Studio

**Official download page:**
[https://developer.android.com/studio](https://developer.android.com/studio)

1. Click the download link on that page.
2. Accept the terms and download the `.exe` installer.
3. Run the installer and follow the Setup Wizard.
4. When the Setup Wizard asks about SDK components, **let it download the defaults** — this gets you the essential SDK platforms, build-tools, platform-tools, and emulator.

> **Disk space needed:** ~8 GB for Studio alone, ~16 GB if you also want the emulator (recommended).

---

## 2. Enable the Command-Line Tools

After Android Studio finishes its initial setup:

1. Open **Android Studio**.
2. Click the **hamburger menu** (≡) in the top-left.
3. Go to **Tools → SDK Manager**.
4. Click the **SDK Tools** tab.
5. Find **"Android SDK Command-line Tools (latest)"** — it's about the third item down.
6. ✅ Check the checkbox next to it.
7. Click **Apply** — Android Studio will download and install it automatically.

This gives you `sdkmanager` and `avdmanager` from the command line, which lets you install SDK packages and create emulators without ever opening Android Studio again.

---

## 3. Set Environment Variables (Optional)

Open **System Properties → Environment Variables** (search "environment variables" in the Windows Start menu).

### System Variables — Create or Edit These

| Variable | Value |
|---|---|
| `JAVA_HOME` | `C:\Program Files\Android\Android Studio\jbr` |
| `ANDROID_HOME` | `%LOCALAPPDATA%\Android\Sdk` |

### Edit the `PATH` Variable — Add These Lines

```
%JAVA_HOME%\bin
%ANDROID_HOME%\platform-tools
%ANDROID_HOME%\emulator
%ANDROID_HOME%\cmdline-tools\latest\bin
```

### Verify It Worked

Close and reopen PowerShell, then run:

```powershell
java -version
sdkmanager --version
adb version
```

If all three return version numbers, you're set.

---

## 4. Create a Pixel 6 Emulator

You can create the emulator entirely from PowerShell — no need to open Android Studio.

### Step 4a: Install a System Image

First, install the Android 35 system image with Google APIs:

```powershell
sdkmanager "system-images;android-35;google_apis;x86_64"
```

Type `y` when prompted to accept the license.

### Step 4b: Create the AVD (Android Virtual Device)

```powershell
avdmanager create avd `
  --name "Pixel_6" `
  --device "pixel_6" `
  --package "system-images;android-35;google_apis;x86_64" `
  --tag "google_apis"
```

When it asks "Do you wish to create a custom hardware profile?", type **no** and press Enter.

### Step 4c: Start the Emulator

```powershell
emulator -avd Pixel_6
```

Or to run it in the background (headless, no window):

```powershell
Start-Process emulator -ArgumentList "-avd","Pixel_6","-no-window" -NoNewWindow
```

### Step 4d: Verify the Emulator Is Running

```powershell
adb devices
```

You should see an `emulator-5554` device listed.

---

## 5. Build and Run an Android App from PowerShell (Let Agent do it)

Once the environment is set up, the full workflow from PowerShell is:

### Create a Project

You can have the AI tool generate the entire project structure (all files: `build.gradle`, `AndroidManifest.xml`, Kotlin/Java source, resources, and the Gradle wrapper).

### Build the APK

```powershell
cd "C:\path\to\your\project"
.\gradlew.bat assembleDebug
```

The APK will be at:

```
app\build\outputs\apk\debug\app-debug.apk
```

### Install on Emulator or Physical Device

```powershell
# Start the emulator first (if not already running)
emulator -avd Pixel_6

# Wait for it to boot, then install
adb install app\build\outputs\apk\debug\app-debug.apk
```

### Install on a Physical Phone (USB)

1. Enable **Developer Options** and **USB Debugging** on your phone.
2. Plug it in via USB.
3. Run `adb devices` to confirm it appears.
4. Run `adb install app\build\outputs\apk\debug\app-debug.apk`.

---

## Quick Reference — Key Commands

| Task | Command |
|---|---|
| List available SDK packages | `sdkmanager --list` |
| Install a platform | `sdkmanager "platforms;android-35"` |
| Install build tools | `sdkmanager "build-tools;35.0.1"` |
| List available device definitions | `avdmanager list device` |
| List existing AVDs | `avdmanager list avd` |
| Delete an AVD | `avdmanager delete avd --name "Pixel_6"` |
| Start emulator | `emulator -avd Pixel_6` |
| List connected devices | `adb devices` |
| Install APK | `adb install app-debug.apk` |
| Uninstall app | `adb uninstall com.example.myapp` |
| View emulator logs | `adb logcat` |
| Build debug APK | `.\gradlew.bat assembleDebug` |
| Build release APK | `.\gradlew.bat assembleRelease` |
| Clean build | `.\gradlew.bat clean` |

---

## What Android Studio Gives You (That You Don't Need to Install Separately)

| Component | Installed By | Location |
|---|---|---|
| JDK 21 (JetBrains Runtime) | Android Studio (bundled) | `C:\Program Files\Android\Android Studio\jbr` |
| Android SDK | Setup Wizard | `%LOCALAPPDATA%\Android\Sdk` |
| Build Tools | Setup Wizard | `%LOCALAPPDATA%\Android\Sdk\build-tools\` |
| Platform Tools (adb) | Setup Wizard | `%LOCALAPPDATA%\Android\Sdk\platform-tools\` |
| Android Platforms | Setup Wizard | `%LOCALAPPDATA%\Android\Sdk\platforms\` |
| Emulator | Setup Wizard | `%LOCALAPPDATA%\Android\Sdk\emulator\` |
| Gradle | Auto-downloaded on first build | `%USERPROFILE%\.gradle\wrapper\dists\` |
| **Command-line Tools** | **You add this manually** (Step 2) | `%LOCALAPPDATA%\Android\Sdk\cmdline-tools\latest\` |

---

## Troubleshooting

### `sdkmanager` is not recognized

- Make sure `%ANDROID_HOME%\cmdline-tools\latest\bin` is in your PATH.
- Make sure you actually installed the command-line tools in Step 2.
- Close and reopen PowerShell after changing environment variables.

### `java -version` shows the wrong version

- Make sure `JAVA_HOME` points to `C:\Program Files\Android\Android Studio\jbr`.
- Make sure `%JAVA_HOME%\bin` is in your PATH **before** any other Java paths.

### Emulator won't start

- Make sure your BIOS has virtualization enabled (Intel VT-x or AMD-V).
- On Windows 11, enable **Windows Hypervisor Platform** or **Hyper-V** in "Turn Windows features on or off".
- Run `emulator -avd Pixel_6 -verbose` to see detailed error messages.

### `adb devices` shows nothing

- The emulator takes time to boot. Wait 30–60 seconds and try again.
- For a physical device, make sure USB Debugging is enabled and you authorized the computer on the phone.

### Gradle build fails with JDK errors

- Confirm `JAVA_HOME` is set to the JetBrains Runtime: `C:\Program Files\Android\Android Studio\jbr`.
- Modern Android Gradle Plugin requires JDK 17+. The bundled JBR is JDK 21, which works.

---

*Last updated: July 2025*