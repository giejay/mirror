# SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
# SPDX-License-Identifier: GPL-3.0-or-later

# Local Windows build of Eden (eden.exe and eden-cli).
#
# Requirements (one-time):
#   - Visual Studio 2022 or newer with "Desktop development with C++"
#     (for -Compiler clang-cl also "C++ Clang tools for Windows")
#   - Git for Windows
#   - Vulkan SDK (run tools/windows/install-vulkan-sdk.ps1 as administrator)
# Qt, SDL3, FFmpeg, Boost etc. are downloaded automatically by CMake on the first configure.
#
# Usage (from any PowerShell):
#   powershell -ExecutionPolicy Bypass -File tools\windows\build-local.ps1
#   ... -BuildType RelWithDebInfo -Compiler msvc -Clean -Target yuzu

param(
    [ValidateSet("Release", "RelWithDebInfo", "Debug")]
    [string]$BuildType = "Release",

    [ValidateSet("auto", "clang-cl", "msvc")]
    [string]$Compiler = "auto",

    [string]$BuildDir = "build",

    # Build only one target, e.g. "yuzu" (eden.exe) or "yuzu-cmd" (eden-cli). Empty = everything.
    [string]$Target = "",

    [int]$Jobs = [Environment]::ProcessorCount,

    # Qt 6.8.3 for MSVC 2022 x64 (the folder containing lib\cmake\Qt6). Empty = look in the usual
    # places, and install it into .cache\qt with aqtinstall (Python) when it isn't there.
    [string]$QtDir = "",

    # Delete the build directory first (needed after switching compiler).
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path "$PSScriptRoot\..\..").Path
Set-Location $RepoRoot

function Fail($message) {
    Write-Host "build-local: $message" -ForegroundColor Red
    exit 1
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Fail "git not found. Install Git for Windows: https://gitforwindows.org"
}

# Vulkan SDK
if (-not $env:VULKAN_SDK -or -not (Test-Path $env:VULKAN_SDK)) {
    $sdk = Get-ChildItem "C:\VulkanSDK" -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($sdk) {
        $env:VULKAN_SDK = $sdk.FullName
    } else {
        Fail "Vulkan SDK not found. Run tools\windows\install-vulkan-sdk.ps1 as administrator, then open a new terminal."
    }
}
Write-Host "build-local: Vulkan SDK: $env:VULKAN_SDK"

# The shader compiler ships in the SDK's Bin folder, which isn't on PATH until a new login.
$vulkanBin = Join-Path $env:VULKAN_SDK "Bin"
if (-not (Get-Command glslangValidator -ErrorAction SilentlyContinue)) {
    $env:PATH = "$vulkanBin;$env:PATH"
}
if (-not (Get-Command glslangValidator -ErrorAction SilentlyContinue)) {
    Fail "glslangValidator.exe not found in $vulkanBin. Reinstall the Vulkan SDK (tools\windows\install-vulkan-sdk.ps1)."
}

# Visual Studio environment (puts cl/clang-cl, and VS's bundled cmake + ninja on PATH)
if (-not $env:VCINSTALLDIR) {
    & "$PSScriptRoot\load-msvc-env.ps1"
    if (-not $env:VCINSTALLDIR) {
        Fail "Could not load the Visual Studio environment."
    }
}

foreach ($tool in @("cmake", "ninja")) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Fail "$tool not found. Install the 'C++ CMake tools for Windows' component in the Visual Studio Installer."
    }
}

if ($Compiler -eq "auto") {
    $Compiler = if (Get-Command clang-cl -ErrorAction SilentlyContinue) { "clang-cl" } else { "msvc" }
}
if ($Compiler -eq "clang-cl") {
    if (-not (Get-Command clang-cl -ErrorAction SilentlyContinue)) {
        Fail "clang-cl not found. Install 'C++ Clang tools for Windows' or use -Compiler msvc."
    }
    $cc = "clang-cl"
} else {
    $cc = "cl"
}
Write-Host "build-local: compiler: $Compiler, build type: $BuildType, dir: $BuildDir"

if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "build-local: removing $BuildDir"
    Remove-Item -Recurse -Force $BuildDir
}

# Switching compiler inside an existing build dir doesn't work; tell the user instead of failing later.
$cache = Join-Path $BuildDir "CMakeCache.txt"
if (Test-Path $cache) {
    $cached = Select-String -Path $cache -Pattern "^CMAKE_CXX_COMPILER:\w+=(.*)$" | Select-Object -First 1
    $cachedName = if ($cached) { [IO.Path]::GetFileNameWithoutExtension($cached.Matches[0].Groups[1].Value) }
    if ($cachedName -and ($cachedName -ne $cc)) {
        Fail "$BuildDir was configured with a different compiler. Re-run with -Clean."
    }
}

# Qt: a shared Qt 6.8.3 (the version Eden suggests for Windows). The Qt CMake downloads by default on Windows is a
# static build without the SVG module, so SVG icons would be missing.
$QtVersion = "6.8.3"
$qtCandidates = @($QtDir, $env:QT_DIR, "C:\Qt\$QtVersion\msvc2022_64",
    (Join-Path $RepoRoot ".cache\qt\$QtVersion\msvc2022_64"))
$QtDir = $qtCandidates | Where-Object { $_ -and (Test-Path (Join-Path $_ "lib\cmake\Qt6")) } |
    Select-Object -First 1
if (-not $QtDir) {
    $python = @("py", "python") | Where-Object { Get-Command $_ -ErrorAction SilentlyContinue } |
        Select-Object -First 1
    if (-not $python) {
        Fail ("Qt $QtVersion not found. Install Python (winget install -e --id Python.Python.3.12) " +
              "so the script can download Qt, or install Qt $QtVersion 'MSVC 2022 64-bit' with the " +
              "Qt installer and pass -QtDir C:\Qt\$QtVersion\msvc2022_64.")
    }
    $qtRoot = Join-Path $RepoRoot ".cache\qt"
    Write-Host "build-local: downloading Qt $QtVersion into $qtRoot (one time)"
    $ErrorActionPreference = "Continue"
    & $python -m pip install --user --quiet --upgrade aqtinstall 2>&1 | Out-Null
    & $python -m aqt install-qt windows desktop $QtVersion win64_msvc2022_64 -m qtcharts -O $qtRoot 2>&1 |
        ForEach-Object { "$_" } | Select-String -Pattern "ERROR|Finished" | ForEach-Object { Write-Host "  $_" }
    $ErrorActionPreference = "Stop"
    $QtDir = Join-Path $qtRoot "$QtVersion\msvc2022_64"
    if (-not (Test-Path (Join-Path $QtDir "lib\cmake\Qt6"))) {
        Fail "Qt download failed. Install Qt $QtVersion 'MSVC 2022 64-bit' yourself and pass -QtDir."
    }
}
Write-Host "build-local: Qt: $QtDir"

# A build folder configured with the bundled (static) Qt keeps pointing at it; reconfigure fresh.
if ((Test-Path $cache) -and (Select-String -Path $cache -Pattern "^YUZU_USE_BUNDLED_QT:BOOL=ON" -Quiet)) {
    Write-Host "build-local: switching from the bundled Qt, resetting the CMake cache"
    Remove-Item -Force $cache
    Remove-Item -Recurse -Force (Join-Path $BuildDir "CMakeFiles") -ErrorAction SilentlyContinue
}

# Code Qt generates (moc/uic/rcc) is only regenerated when its sources change, so after switching
# Qt it still comes from the old version and fails to compile. Drop it when the Qt folder changes.
$qtMarker = Join-Path $BuildDir ".eden-qt-dir"
$previousQt = if (Test-Path $qtMarker) { (Get-Content $qtMarker -Raw).Trim() } else { "" }
if ((Test-Path $BuildDir) -and ($previousQt -ne $QtDir)) {
    Write-Host "build-local: Qt changed, removing Qt-generated code from $BuildDir"
    Get-ChildItem $BuildDir -Recurse -Directory -Filter "*_autogen" -ErrorAction SilentlyContinue |
        Remove-Item -Recurse -Force -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
Set-Content -Path $qtMarker -Value $QtDir

$configureArgs = @(
    "-S", ".", "-B", $BuildDir, "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$BuildType",
    "-DCMAKE_C_COMPILER=$cc",
    "-DCMAKE_CXX_COMPILER=$cc",
    "-DYUZU_TESTS=OFF",
    "-DENABLE_WERROR=OFF",
    "-DENABLE_QT_TRANSLATION=OFF",
    "-DYUZU_USE_BUNDLED_QT=OFF",
    "-DCMAKE_PREFIX_PATH=$QtDir",
    "-DQt6_DIR=$QtDir\lib\cmake\Qt6"
)
if ($Compiler -eq "clang-cl") {
    # With ENABLE_WERROR off the project passes -Wall, which clang-cl treats as -Weverything:
    # hundreds of warnings per file make the build much slower. /w silences warnings (errors stay).
    # _USE_STD_VECTOR_ALGORITHMS=0: the VS 2026 STL sends std::find on 12/16-byte structs that
    # clang reports as trivially comparable into its vectorized path, which static_asserts
    # "unexpected size" (seen in shader_recompiler).
    $configureArgs += @(
        "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /w",
        "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /w /D_USE_STD_VECTOR_ALGORITHMS=0"
    )
}
if (Get-Command sccache -ErrorAction SilentlyContinue) {
    $configureArgs += @("-DCMAKE_C_COMPILER_LAUNCHER=sccache", "-DCMAKE_CXX_COMPILER_LAUNCHER=sccache")
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$configureLog = Join-Path (Resolve-Path $BuildDir) "configure.log"
$logFile = Join-Path (Resolve-Path $BuildDir) "build-local.log"
$errorPattern = "FAILED:|error:|error C\d+|error LNK|fatal error|undefined symbol|CMake Error"

$lineWidth = 100
try { $lineWidth = [Math]::Max(40, $Host.UI.RawUI.WindowSize.Width - 1) } catch {}

# Runs a command, writes everything to $Log and shows only ninja's [n/total] progress on one line
# plus error lines. Native stderr becomes error records in Windows PowerShell, so the output is
# turned into plain strings and the error preference relaxed while it runs.
function Invoke-Quiet([string[]]$Arguments, [string]$Log) {
    $ErrorActionPreference = "Continue"
    & cmake @Arguments 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $Log | ForEach-Object {
        if ($_ -match '^\[(\d+)/(\d+)\]\s*(.*)$') {
            $text = "[$($matches[1])/$($matches[2])] $($matches[3])"
            if ($text.Length -gt $lineWidth) { $text = $text.Substring(0, $lineWidth) }
            Write-Host -NoNewline ("`r" + $text.PadRight($lineWidth))
        } elseif ($_ -match $errorPattern) {
            Write-Host ""
            Write-Host $_ -ForegroundColor Red
        }
    }
    $code = $LASTEXITCODE
    Write-Host ""
    return $code
}

function Show-Errors([string]$Log) {
    Write-Host "build-local: errors (first 30 lines):" -ForegroundColor Red
    Select-String -Path $Log -Pattern $errorPattern | Select-Object -First 30 |
        ForEach-Object { Write-Host "  $($_.Line)" }
}

Write-Host "build-local: configuring (the first run downloads dependencies and takes a while)"
if ((Invoke-Quiet $configureArgs $configureLog) -ne 0) {
    Show-Errors $configureLog
    Fail "configure failed. Full output: $configureLog. If it keeps failing, try -Clean, or delete .cache\cpm."
}

$buildArgs = @("--build", $BuildDir, "--parallel", $Jobs)
if ($Target) {
    $buildArgs += @("--target", $Target)
}
# Keep compiling the other files after an error, so one run shows every failing file.
$buildArgs += @("--", "-k", "0")
Write-Host "build-local: building with $Jobs jobs (full output in $logFile)"
if ((Invoke-Quiet $buildArgs $logFile) -ne 0) {
    Show-Errors $logFile
    Fail "build failed. Full output: $logFile"
}

$binDir = Join-Path (Resolve-Path $BuildDir) "bin"
Write-Host ""
Write-Host "build-local: done. Executables in $binDir" -ForegroundColor Green
Get-ChildItem $binDir -Filter *.exe -ErrorAction SilentlyContinue | ForEach-Object {
    Write-Host "  $($_.FullName)"
}
