# Java + Kotlin + Gradle Development - Clean System Setup Guide

> **Goal:** Starting from a fresh Windows machine, set up a complete JVM development environment for Java, Kotlin, Gradle, Spring Boot, Android-adjacent work, CLI tools, services, and libraries. The setup is written so an AI coding agent can install the tooling from PowerShell, inspect Gradle projects, and build/test reliably.

---

## What You'll End Up With

| Tool | What It Does | Installed By |
|---|---|---|
| **Eclipse Temurin JDK 25** | Current Java LTS JDK for compiling/running Java and Kotlin JVM projects | winget |
| **Gradle** | JVM build tool used heavily by Kotlin, Android, Spring, and multi-module projects | Official Gradle ZIP |
| **Gradle Wrapper** | Project-pinned Gradle launcher, preferred for repository builds | `gradle wrapper` or existing project |
| **Kotlin Gradle Plugin** | Compiles Kotlin through Gradle without requiring a global Kotlin compiler | Gradle project dependency |
| **Kotlin CLI compiler** | Optional standalone `kotlinc` for quick scripts and experiments | Official Kotlin ZIP |
| **IntelliJ IDEA Community** | Full Java/Kotlin IDE with Gradle import and project tooling | winget or browser |
| **VS Code** | Lightweight editor with Java/Kotlin/Gradle extensions | winget |
| **Git** | Version control | winget |

---

## Why Java/Kotlin + Gradle?

This environment is useful because it covers:

- Java services, libraries, CLIs, and enterprise codebases.
- Kotlin JVM applications and libraries.
- Gradle builds, including `build.gradle` and `build.gradle.kts`.
- Spring Boot and JVM backend projects.
- Android-adjacent skills, since Android projects are Gradle/Kotlin-heavy.
- Multi-module builds where agents need deterministic `.\gradlew.bat` commands.

For Android app development, use this guide together with the Android guide in this folder. Android Studio usually supplies its own JDK/JBR, and Android Gradle Plugin compatibility may require a different JDK than general Java work.

---

## JDK Version Choice

Use **JDK 25 LTS** for new general Java/Kotlin projects. It is the current LTS line as of this guide.

Use **JDK 21 LTS** instead when:

- A repository's `gradle.properties`, toolchains, or CI config requires Java 21.
- Android tooling in the project does not support the newer JDK.
- A framework or plugin has not certified Java 25 yet.

Gradle projects can declare Java toolchains, so a repository may build with one JDK even if another JDK is installed globally.

---

## Choose an Install Path

| Path | Best For | IDE Included |
|---|---|---|
| **A. Agent-friendly CLI install** | Models, automation, CI-like local setup | VS Code and optional IntelliJ |
| **B. IntelliJ IDEA Community** | Humans working on Java/Kotlin/Gradle projects | Yes |
| **C. Browser/manual install** | Humans who want installers and UI choices | Yes |

For an AI coding agent, Path A is the important one.

---

## Path A - Agent-Friendly PowerShell Install

Run this in an Administrator PowerShell window:

```powershell
# ============================================================
# Java + Kotlin + Gradle Development Environment
# Run in Administrator PowerShell
# ============================================================

Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

# 1. Install JDK 25 LTS, Git, VS Code, and IntelliJ IDEA Community.
winget install --id EclipseAdoptium.Temurin.25.JDK --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
winget install --id Git.Git --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
winget install --id Microsoft.VisualStudioCode --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
winget install --id JetBrains.IntelliJIDEA.Community --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements

# 2. Find the installed JDK and configure JAVA_HOME.
$jdkHome = Get-ChildItem "${env:ProgramFiles}\Eclipse Adoptium" -Directory -Filter "jdk-25*" |
    Sort-Object Name -Descending |
    Select-Object -First 1 -ExpandProperty FullName

if (-not $jdkHome) {
    throw "JDK 25 was not found under $env:ProgramFiles\Eclipse Adoptium"
}

[Environment]::SetEnvironmentVariable("JAVA_HOME", $jdkHome, "User")

$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$javaBin = Join-Path $jdkHome "bin"
if ($userPath -notlike "*$javaBin*") {
    $userPath = "$userPath;$javaBin"
}

# 3. Install Gradle from the official distribution ZIP.
$gradleVersion = "9.5.1"
$gradleRoot = "C:\Gradle"
$gradleZip = Join-Path $env:TEMP "gradle-$gradleVersion-bin.zip"
$gradleUrl = "https://services.gradle.org/distributions/gradle-$gradleVersion-bin.zip"

New-Item -ItemType Directory -Force -Path $gradleRoot | Out-Null
Invoke-WebRequest -Uri $gradleUrl -OutFile $gradleZip
Expand-Archive -LiteralPath $gradleZip -DestinationPath $gradleRoot -Force
Remove-Item -LiteralPath $gradleZip -Force

$gradleHome = Join-Path $gradleRoot "gradle-$gradleVersion"
$gradleBin = Join-Path $gradleHome "bin"
[Environment]::SetEnvironmentVariable("GRADLE_HOME", $gradleHome, "User")
if ($userPath -notlike "*$gradleBin*") {
    $userPath = "$userPath;$gradleBin"
}

[Environment]::SetEnvironmentVariable("Path", $userPath, "User")

# Refresh this PowerShell session.
$env:JAVA_HOME = $jdkHome
$env:GRADLE_HOME = $gradleHome
$env:Path = "$env:Path;$javaBin;$gradleBin"

# 4. Install VS Code extensions.
code --install-extension vscjava.vscode-java-pack
code --install-extension vscjava.vscode-gradle
code --install-extension fwcd.kotlin

# 5. Verify.
Write-Host "`n========== Java/Kotlin/Gradle Installation Summary ==========" -ForegroundColor Cyan
Write-Host "JAVA_HOME:   $env:JAVA_HOME" -ForegroundColor Green
java -version
javac -version
gradle --version
git --version
Write-Host "=============================================================" -ForegroundColor Cyan
Write-Host "`nClose and reopen PowerShell so JAVA_HOME and GRADLE_HOME persist cleanly." -ForegroundColor Yellow
```

Close and reopen PowerShell after the script finishes.

---

## Path B - IntelliJ IDEA Community

IntelliJ IDEA Community is the recommended full IDE for Java/Kotlin/Gradle projects.

Install from PowerShell:

```powershell
winget install --id JetBrains.IntelliJIDEA.Community --exact --silent `
  --accept-source-agreements `
  --accept-package-agreements
```

Or download manually:

[https://www.jetbrains.com/idea/download/](https://www.jetbrains.com/idea/download/)

Use IntelliJ when:

- Importing large Gradle projects.
- Editing Kotlin with rich refactoring.
- Working with Gradle Kotlin DSL.
- Debugging JVM apps.
- Working on desktop/server Java code where IDE navigation matters.

For Android projects, use Android Studio instead.

---

## Path C - Manual Downloads

| Tool | Download Page |
|---|---|
| Eclipse Temurin JDK | [https://adoptium.net/temurin/releases/](https://adoptium.net/temurin/releases/) |
| Gradle | [https://gradle.org/install/](https://gradle.org/install/) |
| Kotlin compiler | [https://kotlinlang.org/docs/command-line.html](https://kotlinlang.org/docs/command-line.html) |
| IntelliJ IDEA Community | [https://www.jetbrains.com/idea/download/](https://www.jetbrains.com/idea/download/) |
| VS Code | [https://code.visualstudio.com](https://code.visualstudio.com) |
| Git | [https://git-scm.com/download/win](https://git-scm.com/download/win) |

---

## Optional - Install Standalone Kotlin CLI

Most Kotlin projects should compile through Gradle. Install standalone `kotlinc` only when you want quick one-file Kotlin scripts or direct compiler experiments.

```powershell
$kotlinVersion = "2.4.0"
$kotlinRoot = "C:\Kotlin"
$kotlinZip = Join-Path $env:TEMP "kotlin-compiler-$kotlinVersion.zip"
$kotlinUrl = "https://github.com/JetBrains/kotlin/releases/download/v$kotlinVersion/kotlin-compiler-$kotlinVersion.zip"

New-Item -ItemType Directory -Force -Path $kotlinRoot | Out-Null
Invoke-WebRequest -Uri $kotlinUrl -OutFile $kotlinZip
Expand-Archive -LiteralPath $kotlinZip -DestinationPath $kotlinRoot -Force
Remove-Item -LiteralPath $kotlinZip -Force

$kotlinBin = Join-Path $kotlinRoot "kotlinc\bin"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$kotlinBin*") {
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$kotlinBin", "User")
}
if ($env:Path -notlike "*$kotlinBin*") {
    $env:Path = "$env:Path;$kotlinBin"
}

kotlinc -version
```

---

## Create and Verify Projects from PowerShell

### Java Application with Gradle

```powershell
mkdir C:\Code
cd C:\Code

gradle init `
  --type java-application `
  --dsl kotlin `
  --test-framework junit-jupiter `
  --project-name hello-java `
  --package com.example.hellojava `
  --java-version 25 `
  --no-split-project `
  --incubating

cd hello-java
.\gradlew.bat run
.\gradlew.bat test
.\gradlew.bat build
```

Use the generated `.\gradlew.bat`, not the global `gradle`, once a project has a wrapper.

### Kotlin JVM Application with Gradle

```powershell
cd C:\Code

gradle init `
  --type kotlin-application `
  --dsl kotlin `
  --test-framework kotlin-test `
  --project-name hello-kotlin `
  --package com.example.hellokotlin `
  --java-version 25 `
  --no-split-project `
  --incubating

cd hello-kotlin
.\gradlew.bat run
.\gradlew.bat test
.\gradlew.bat build
```

If the selected test framework is not available in the installed Gradle version, run:

```powershell
gradle init --help
```

and choose one of the listed test framework names.

### Minimal Kotlin Gradle Build

This is a small known-good Kotlin JVM setup an agent can create or recognize.

`settings.gradle.kts`:

```kotlin
pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
        google()
    }
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        mavenCentral()
        google()
    }
}

rootProject.name = "hello-kotlin"
```

`build.gradle.kts`:

```kotlin
plugins {
    kotlin("jvm") version "2.4.0"
    application
}

repositories {
    mavenCentral()
}

dependencies {
    testImplementation(kotlin("test"))
}

application {
    mainClass.set("com.example.MainKt")
}

tasks.test {
    useJUnitPlatform()
}
```

`src/main/kotlin/com/example/Main.kt`:

```kotlin
package com.example

fun main() {
    println("Hello from Kotlin + Gradle!")
}
```

Run:

```powershell
.\gradlew.bat run
.\gradlew.bat test
```

### Spring Boot Project

Use Spring Initializr for the cleanest generated project:

```powershell
cd C:\Code

$uri = "https://start.spring.io/starter.zip?type=gradle-project-kotlin&language=kotlin&baseDir=demo-api&groupId=com.example&artifactId=demo-api&name=demo-api&packageName=com.example.demoapi&javaVersion=25&dependencies=web,actuator,validation"
$zip = Join-Path $env:TEMP "demo-api.zip"

Invoke-WebRequest -Uri $uri -OutFile $zip
Expand-Archive -LiteralPath $zip -DestinationPath C:\Code -Force
Remove-Item -LiteralPath $zip -Force

cd demo-api
.\gradlew.bat bootRun
```

If `javaVersion=25` is not accepted by Spring Initializr for the selected Spring Boot line, use `javaVersion=21`.

---

## Gradle Wrapper Rules

The Gradle Wrapper is the project-local build launcher:

```text
gradlew
gradlew.bat
gradle/wrapper/gradle-wrapper.jar
gradle/wrapper/gradle-wrapper.properties
```

Agent rules:

- If `gradlew.bat` exists, use `.\gradlew.bat`, not global `gradle`.
- Commit the wrapper files for applications and libraries unless the repository explicitly excludes them.
- Do not change the wrapper version casually.
- To update the wrapper intentionally:

```powershell
.\gradlew.bat wrapper --gradle-version 9.5.1 --distribution-type bin
```

Then verify:

```powershell
.\gradlew.bat --version
.\gradlew.bat build
```

---

## Common Project Files

| File | What the Agent Should Know |
|---|---|
| `settings.gradle` | Groovy settings file; module includes and plugin management |
| `settings.gradle.kts` | Kotlin DSL settings file |
| `build.gradle` | Groovy Gradle build script |
| `build.gradle.kts` | Kotlin DSL Gradle build script |
| `gradle.properties` | JVM args, Gradle flags, Kotlin options, project properties |
| `gradle/libs.versions.toml` | Version catalog for dependencies and plugins |
| `gradle/wrapper/gradle-wrapper.properties` | Pinned Gradle wrapper version |
| `src/main/java` | Java production sources |
| `src/main/kotlin` | Kotlin production sources |
| `src/test/java` | Java tests |
| `src/test/kotlin` | Kotlin tests |

---

## Recommended Gradle Defaults

### `gradle.properties`

```properties
org.gradle.jvmargs=-Xmx2g -Dfile.encoding=UTF-8
org.gradle.caching=true
org.gradle.parallel=true
kotlin.code.style=official
```

For large projects, increase `-Xmx` only after measuring memory pressure.

### Java Toolchain in `build.gradle.kts`

```kotlin
java {
    toolchain {
        languageVersion.set(JavaLanguageVersion.of(25))
    }
}
```

Use `21` when a project or plugin requires Java 21.

### Kotlin JVM Toolchain

```kotlin
kotlin {
    jvmToolchain(25)
}
```

Keep Java and Kotlin target versions aligned unless the project has a documented reason not to.

---

## Agent Programming Contract for Java/Kotlin/Gradle

Before editing:

```powershell
java -version
javac -version
gradle --version
if (Test-Path .\gradlew.bat) { .\gradlew.bat --version }
if (Test-Path .\gradlew.bat) { .\gradlew.bat projects }
```

Read whichever of these exist:

```text
settings.gradle
settings.gradle.kts
build.gradle
build.gradle.kts
gradle.properties
gradle/libs.versions.toml
gradle/wrapper/gradle-wrapper.properties
src/main/java/
src/main/kotlin/
src/test/java/
src/test/kotlin/
pom.xml
```

During coding:

- Use `.\gradlew.bat` when present.
- Prefer Gradle Kotlin DSL changes in `*.gradle.kts` projects and Groovy DSL changes in `*.gradle` projects.
- Keep dependency versions in `gradle/libs.versions.toml` when the project uses a version catalog.
- Prefer Java/Kotlin toolchains over changing machine-global `JAVA_HOME`.
- Do not upgrade Gradle, Kotlin, Spring Boot, or Android Gradle Plugin casually.
- Keep Java package names and directory paths aligned.
- Keep Kotlin package declarations and directory layout sensible, even though Kotlin is less strict.
- Use JUnit 5 for new JVM tests unless the project uses another framework.
- For Android projects, follow the Android guide and project AGP/Kotlin compatibility matrix.

Standard verification:

```powershell
.\gradlew.bat clean
.\gradlew.bat test
.\gradlew.bat build
```

If no wrapper exists:

```powershell
gradle clean test build
```

For Spring Boot:

```powershell
.\gradlew.bat bootRun
```

Done criteria:

- The selected JDK is compatible with the project.
- Gradle sync/build scripts are valid.
- `test` passes.
- `build` passes.
- New dependencies are declared in the project's established place.
- Wrapper/version changes are intentional and documented.
- README or project docs mention new JDK, Gradle, Kotlin, or framework requirements.

---

## Quick Reference - Daily Commands

| Task | Command |
|---|---|
| Show Java version | `java -version` |
| Show compiler version | `javac -version` |
| Show global Gradle version | `gradle --version` |
| Show wrapper Gradle version | `.\gradlew.bat --version` |
| List Gradle tasks | `.\gradlew.bat tasks` |
| List projects/modules | `.\gradlew.bat projects` |
| Build | `.\gradlew.bat build` |
| Test | `.\gradlew.bat test` |
| Clean | `.\gradlew.bat clean` |
| Run app | `.\gradlew.bat run` |
| Spring Boot run | `.\gradlew.bat bootRun` |
| Generate wrapper | `gradle wrapper` |
| Update wrapper | `.\gradlew.bat wrapper --gradle-version 9.5.1 --distribution-type bin` |
| Java app init | `gradle init --type java-application --dsl kotlin` |
| Kotlin app init | `gradle init --type kotlin-application --dsl kotlin` |
| Kotlin compiler version | `kotlinc -version` |

---

## Troubleshooting

### `java` or `javac` is not recognized

Close and reopen PowerShell. If it still fails, repair the user PATH:

```powershell
$jdkHome = Get-ChildItem "${env:ProgramFiles}\Eclipse Adoptium" -Directory -Filter "jdk-25*" |
    Sort-Object Name -Descending |
    Select-Object -First 1 -ExpandProperty FullName

[Environment]::SetEnvironmentVariable("JAVA_HOME", $jdkHome, "User")
$javaBin = Join-Path $jdkHome "bin"
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$javaBin*") {
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$javaBin", "User")
}
$env:JAVA_HOME = $jdkHome
$env:Path = "$env:Path;$javaBin"

java -version
javac -version
```

### `gradle` is not recognized

Close and reopen PowerShell. If it still fails:

```powershell
$gradleHome = Get-ChildItem "C:\Gradle" -Directory -Filter "gradle-*" |
    Sort-Object Name -Descending |
    Select-Object -First 1 -ExpandProperty FullName

$gradleBin = Join-Path $gradleHome "bin"
[Environment]::SetEnvironmentVariable("GRADLE_HOME", $gradleHome, "User")
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
if ($userPath -notlike "*$gradleBin*") {
    [Environment]::SetEnvironmentVariable("Path", "$userPath;$gradleBin", "User")
}
$env:GRADLE_HOME = $gradleHome
$env:Path = "$env:Path;$gradleBin"

gradle --version
```

### Project fails with "Unsupported class file major version"

The runtime JDK, Gradle version, or plugin version is too old for the bytecode. Check:

```powershell
java -version
.\gradlew.bat --version
```

Use the JDK expected by the project, or update Gradle/plugins intentionally.

### Gradle daemon behaves strangely after JDK changes

Stop daemons and rebuild:

```powershell
.\gradlew.bat --stop
.\gradlew.bat clean build
```

### Kotlin DSL script fails after plugin changes

Clear Gradle's script cache only after normal `clean` fails:

```powershell
.\gradlew.bat --stop
Remove-Item -LiteralPath "$env:USERPROFILE\.gradle\caches" -Recurse -Force
.\gradlew.bat build
```

This forces dependencies and scripts to be downloaded again.

### IntelliJ cannot import the Gradle project

Open the project root, not only a subfolder. The root should contain `settings.gradle`, `settings.gradle.kts`, or `gradlew.bat`.

Then use **Reload All Gradle Projects** in IntelliJ's Gradle tool window.

### Android Gradle project fails with JDK 25

Android projects may require JDK 21 or the Android Studio bundled JBR. Use the Android guide in this folder and check the repository's Android Gradle Plugin version.

---

## Official References

| Topic | Link |
|---|---|
| Eclipse Temurin install | [https://adoptium.net/installation/](https://adoptium.net/installation/) |
| Temurin releases | [https://adoptium.net/temurin/releases/](https://adoptium.net/temurin/releases/) |
| Gradle installation | [https://docs.gradle.org/current/userguide/installation.html](https://docs.gradle.org/current/userguide/installation.html) |
| Gradle Wrapper | [https://docs.gradle.org/current/userguide/gradle_wrapper.html](https://docs.gradle.org/current/userguide/gradle_wrapper.html) |
| Gradle build init plugin | [https://docs.gradle.org/current/userguide/build_init_plugin.html](https://docs.gradle.org/current/userguide/build_init_plugin.html) |
| Kotlin command-line compiler | [https://kotlinlang.org/docs/command-line.html](https://kotlinlang.org/docs/command-line.html) |
| Kotlin with Gradle | [https://kotlinlang.org/docs/gradle.html](https://kotlinlang.org/docs/gradle.html) |
| IntelliJ IDEA download | [https://www.jetbrains.com/idea/download/](https://www.jetbrains.com/idea/download/) |
| IntelliJ Gradle support | [https://www.jetbrains.com/help/idea/gradle.html](https://www.jetbrains.com/help/idea/gradle.html) |
| Spring Initializr | [https://start.spring.io/](https://start.spring.io/) |

---

*Last updated: June 2026*
