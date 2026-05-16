# =============================================================================
# POWSYS365 - Windows Installer Creation Script
# =============================================================================
# Script Name : create_windows_installer.ps1
# Version     : 3.0.0
# Author      : XNOX L.L.C.
# Description : Full build and packaging pipeline for POWSYS365 on Windows.
#               Detects Visual Studio 2022, configures CMake with vcpkg,
#               builds all 23 modules in Release x64, deploys Qt6 DLLs,
#               packages Python runtime, and generates both an Inno Setup
#               installer (.exe) and a portable .zip archive.
#
# Usage:
#   .\create_windows_installer.ps1
#   .\create_windows_installer.ps1 -SkipBuild -SkipInstaller
#   .\create_windows_installer.ps1 -BuildType RelWithDebInfo
#
# Requirements:
#   - Windows 10/11 x64
#   - Visual Studio 2022 or Build Tools 2022
#   - CMake 3.31+
#   - vcpkg (with VCPKG_ROOT env var set)
#   - Qt6 6.5+ (with QT6_DIR env var set)
#   - Inno Setup 6 (optional, for .exe installer)
#   - Python 3.11+ (for Python bindings)
#   - 7-Zip or PowerShell 5+ (for .zip creation)
# =============================================================================

[CmdletBinding()]
param(
    [string]$BuildType = "Release",
    [string]$VcpkgRoot = "",
    [string]$Qt6Dir = "",
    [string]$InnoSetupDir = "",
    [string]$OutputDir = "",
    [switch]$SkipBuild = $false,
    [switch]$SkipInstaller = $false,
    [switch]$SkipZip = $false,
    [switch]$Clean = $false,
    [switch]$VerboseCMake = $false
)

# =============================================================================
# Configuration & Constants
# =============================================================================
$ScriptVersion = "3.0.0"
$ProductName = "POWSYS365"
$Publisher = "XNOX L.L.C."
$ProductVersion = "3.0.0"
$Arch = "x64"

# Detect project root (2 levels up from scripts/windows)
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$ScriptDir = (Resolve-Path $PSScriptRoot).Path

# Default output directory
if (-not $OutputDir) {
    $OutputDir = Join-Path $ProjectRoot "build-windows"
}
$BuildDir = Join-Path $OutputDir "build"
$PackageDir = Join-Path $OutputDir "package"
$DistDir = Join-Path $OutputDir "dist"

# =============================================================================
# Console Helpers
# =============================================================================
function Write-Header {
    param([string]$Message)
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Cyan
    Write-Host "  $Message" -ForegroundColor Cyan
    Write-Host "========================================" -ForegroundColor Cyan
}

function Write-Step {
    param([string]$Message)
    Write-Host "  [+] $Message" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Message)
    Write-Host "  [!] WARNING: $Message" -ForegroundColor Yellow
}

function Write-Error {
    param([string]$Message)
    Write-Host "  [X] ERROR: $Message" -ForegroundColor Red
}

function Test-Command {
    param([string]$Command)
    $null = Get-Command $Command -ErrorAction SilentlyContinue
    return $?
}

function Exit-WithError {
    param([string]$Message, [int]$Code = 1)
    Write-Error $Message
    exit $Code
}

# =============================================================================
# Step 1: Print Banner & Validate Environment
# =============================================================================
Write-Host ""
Write-Host "    ____  ____  _    __ ____   ___  ____  _  ___  " -ForegroundColor Blue
Write-Host "   / __ \/ __ \| |  / // __ \ /   |/ __ \/ |/ / / " -ForegroundColor Blue
Write-Host "  / /_/ / / / /| | / // /_/ // /| // / / /   / /  " -ForegroundColor Blue
Write-Host " / ____/ /_/ / | |/ // _, _// ___ |/ /_/ /   | |  " -ForegroundColor Blue
Write-Host "/_/    \____/  |___//_/ |_/_/  |_|\____/_/|_|_|  " -ForegroundColor Blue
Write-Host "                                                 " -ForegroundColor Blue
Write-Host "  Windows Installer Creation Script v$ScriptVersion" -ForegroundColor Blue
Write-Host "  (c) 2025 $Publisher" -ForegroundColor Blue
Write-Host ""

# Validate we're on Windows
if (-not $IsWindows -and -not $env:OS) {
    Exit-WithError "This script must be run on Windows 10/11 x64."
}

# Validate PowerShell version
if ($PSVersionTable.PSVersion.Major -lt 5) {
    Exit-WithError "PowerShell 5.0 or later is required. Current: $($PSVersionTable.PSVersion)"
}

# =============================================================================
# Step 2: Detect Visual Studio 2022
# =============================================================================
Write-Header "Detecting Visual Studio 2022"

$VsWherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $VsWherePath)) {
    $VsWherePath = "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
}

$VsInstallPath = $null
$VcvarsPath = $null

if (Test-Path $VsWherePath) {
    Write-Step "Found vswhere.exe at: $VsWherePath"
    $VsInstances = & $VsWherePath -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -sort -format json | ConvertFrom-Json
    if ($VsInstances -and $VsInstances.Count -gt 0) {
        $VsInstall = $VsInstances | Select-Object -First 1
        $VsInstallPath = $VsInstall.installationPath
        Write-Step "Found Visual Studio 2022 at: $VsInstallPath"
        $VcvarsPath = Join-Path $VsInstallPath "VC\Auxiliary\Build\vcvars64.bat"
        if (Test-Path $VcvarsPath) {
            Write-Step "Found vcvars64.bat"
        } else {
            $VcvarsPath = Join-Path $VsInstallPath "VC\Auxiliary\Build\vcvarsall.bat"
            if (Test-Path $VcvarsPath) {
                Write-Step "Found vcvarsall.bat (will use x64 arg)"
            } else {
                Exit-WithError "Could not find vcvars script in Visual Studio installation."
            }
        }
    }
}

# Fallback: search common locations
if (-not $VsInstallPath) {
    Write-Warn "vswhere.exe not found or no VS2022 instance. Searching common paths..."
    $CommonPaths = @(
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Enterprise",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Professional",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\Community",
        "${env:ProgramFiles}\Microsoft Visual Studio\2022\BuildTools",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Enterprise",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Professional",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\Community",
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools"
    )
    foreach ($Path in $CommonPaths) {
        if (Test-Path $Path) {
            $VsInstallPath = $Path
            $VcvarsPath = Join-Path $Path "VC\Auxiliary\Build\vcvars64.bat"
            if (Test-Path $VcvarsPath) {
                Write-Step "Found Visual Studio 2022 at: $VsInstallPath"
                break
            }
        }
    }
}

if (-not $VsInstallPath) {
    Exit-WithError @"
Visual Studio 2022 or Build Tools 2022 was not found.

Please install one of the following:
  - Visual Studio 2022 Community/Professional/Enterprise
  - Visual Studio Build Tools 2022

Required C++ workload:
  - MSVC v143 - VS 2022 C++ x64/x86 build tools
  - Windows SDK

Download: https://visualstudio.microsoft.com/downloads/
"@
}

# =============================================================================
# Step 3: Detect CMake
# =============================================================================
Write-Header "Detecting CMake"

$CMakeCmd = $null
$CMakeMinVersion = [Version]::new(3, 31, 0)

# Check PATH first
if (Test-Command "cmake") {
    $CMakeVersionStr = (cmake --version) | Select-Object -First 1
    $CMakeVersion = [Version]($CMakeVersionStr -replace 'cmake version ', '' -replace ' CMake.*$', '')
    Write-Step "Found CMake $CMakeVersion in PATH"
    if ($CMakeVersion -ge $CMakeMinVersion) {
        $CMakeCmd = "cmake"
    } else {
        Write-Warn "CMake $CMakeVersion is too old. Minimum required: $CMakeMinVersion"
    }
}

# Check common CMake installations
if (-not $CMakeCmd) {
    $CMakePaths = @(
        "${env:ProgramFiles}\CMake\bin\cmake.exe",
        "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe",
        "${env:LOCALAPPDATA}\Programs\CMake\bin\cmake.exe",
        "${env:LOCALAPPDATA}\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\bin\cmake.exe"
    )
    foreach ($Path in $CMakePaths) {
        if (Test-Path $Path) {
            $CMakeVersionStr = (& $Path --version) | Select-Object -First 1
            $CMakeVersion = [Version]($CMakeVersionStr -replace 'cmake version ', '' -replace ' CMake.*$', '')
            if ($CMakeVersion -ge $CMakeMinVersion) {
                $CMakeCmd = $Path
                Write-Step "Found CMake $CMakeVersion at: $Path"
                break
            }
        }
    }
}

if (-not $CMakeCmd) {
    Exit-WithError @"
CMake 3.31+ was not found.

Install via winget:
    winget install Kitware.CMake

Or download from: https://cmake.org/download/
"@
}

# =============================================================================
# Step 4: Detect vcpkg
# =============================================================================
Write-Header "Detecting vcpkg"

$VcpkgExe = $null

# Use provided VcpkgRoot
if ($VcpkgRoot) {
    $VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
    if (Test-Path $VcpkgExe) {
        Write-Step "Using provided vcpkg at: $VcpkgExe"
    } else {
        Exit-WithError "Provided VcpkgRoot does not contain vcpkg.exe: $VcpkgRoot"
    }
}

# Check VCPKG_ROOT environment variable
if (-not $VcpkgExe) {
    if ($env:VCPKG_ROOT -and (Test-Path (Join-Path $env:VCPKG_ROOT "vcpkg.exe"))) {
        $VcpkgRoot = $env:VCPKG_ROOT
        $VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
        Write-Step "Found vcpkg via VCPKG_ROOT: $VcpkgRoot"
    }
}

# Check common locations
if (-not $VcpkgExe) {
    $VcpkgPaths = @(
        "C:\vcpkg\vcpkg.exe",
        "${env:LOCALAPPDATA}\vcpkg\vcpkg.exe",
        "${env:USERPROFILE}\vcpkg\vcpkg.exe",
        "${env:ProgramFiles}\vcpkg\vcpkg.exe",
        "${env:DEVTOOLS}\vcpkg\vcpkg.exe"
    )
    foreach ($Path in $VcpkgPaths) {
        if (Test-Path $Path) {
            $VcpkgExe = $Path
            $VcpkgRoot = Split-Path $Path -Parent
            Write-Step "Found vcpkg at: $VcpkgRoot"
            break
        }
    }
}

if (-not $VcpkgExe) {
    Write-Warn @"
vcpkg was not found.

Please install vcpkg and set VCPKG_ROOT environment variable:
    git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
    cd C:\vcpkg
    .\bootstrap-vcpkg.bat
    [Environment]::SetEnvironmentVariable('VCPKG_ROOT', 'C:\vcpkg', 'User')

vcpkg is required for Eigen3, nlohmann-json, Catch2, libpq, and zlib.
"@
    # Continue anyway - CMake FetchContent may handle some deps
    $VcpkgRoot = ""
    $VcpkgExe = ""
} else {
    # Verify vcpkg can run
    try {
        $VcpkgVersion = & $VcpkgExe version 2>$null
        Write-Step "vcpkg version: $VcpkgVersion"
    } catch {
        Write-Warn "vcpkg.exe exists but may not work correctly"
    }

    # Verify vcpkg.json exists
    $VcpkgJson = Join-Path $ProjectRoot "vcpkg.json"
    if (Test-Path $VcpkgJson) {
        Write-Step "Found vcpkg.json manifest"
    } else {
        Write-Warn "vcpkg.json not found in project root"
    }
}

# =============================================================================
# Step 5: Detect Qt6
# =============================================================================
Write-Header "Detecting Qt6"

$Qt6BinDir = $null
$WinDeployQt = $null

# Use provided Qt6Dir
if ($Qt6Dir) {
    $TestWinDeploy = Join-Path $Qt6Dir "bin\windeployqt6.exe"
    if (Test-Path $TestWinDeploy) {
        $Qt6BinDir = Join-Path $Qt6Dir "bin"
        $WinDeployQt = $TestWinDeploy
        Write-Step "Using provided Qt6 at: $Qt6Dir"
    } else {
        $TestWinDeploy = Join-Path $Qt6Dir "windeployqt6.exe"
        if (Test-Path $TestWinDeploy) {
            $Qt6BinDir = $Qt6Dir
            $WinDeployQt = $TestWinDeploy
            Write-Step "Using provided Qt6 bin at: $Qt6Dir"
        } else {
            Exit-WithError "Provided Qt6Dir does not contain windeployqt6.exe"
        }
    }
}

# Check QT6_DIR / QTDIR environment variables
if (-not $WinDeployQt) {
    $Qt6EnvPaths = @($env:QT6_DIR, $env:QTDIR, $env:QT_DIR)
    foreach ($Path in $Qt6EnvPaths) {
        if (-not $Path) { continue }
        $TestWinDeploy = Join-Path $Path "bin\windeployqt6.exe"
        if (Test-Path $TestWinDeploy) {
            $Qt6BinDir = Join-Path $Path "bin"
            $WinDeployQt = $TestWinDeploy
            $Qt6Dir = $Path
            Write-Step "Found Qt6 via environment variable: $Path"
            break
        }
    }
}

# Check aqtinstall / common Qt paths
if (-not $WinDeployQt) {
    $QtPaths = @(
        "${env:USERPROFILE}\Qt\6.8.2\msvc2022_64",
        "${env:USERPROFILE}\Qt\6.8.1\msvc2022_64",
        "${env:USERPROFILE}\Qt\6.8.0\msvc2022_64",
        "${env:USERPROFILE}\Qt\6.7.3\msvc2022_64",
        "${env:USERPROFILE}\Qt\6.7.2\msvc2022_64",
        "${env:USERPROFILE}\Qt\6.6.3\msvc2022_64",
        "${env:USERPROFILE}\Qt\6.6.2\msvc2022_64",
        "${env:USERPROFILE}\Qt\6.5.3\msvc2022_64",
        "${env:USERPROFILE}\Qt\6.5.2\msvc2022_64",
        "C:\Qt\6.8.2\msvc2022_64",
        "C:\Qt\6.8.1\msvc2022_64",
        "C:\Qt\6.8.0\msvc2022_64",
        "C:\Qt\6.7.3\msvc2022_64",
        "C:\Qt\6.7.2\msvc2022_64",
        "C:\Qt\6.6.3\msvc2022_64",
        "C:\Qt\6.6.2\msvc2022_64",
        "C:\Qt\6.5.3\msvc2022_64",
        "C:\Qt\6.5.2\msvc2022_64"
    )
    foreach ($Path in $QtPaths) {
        if (Test-Path $Path) {
            $TestWinDeploy = Join-Path $Path "bin\windeployqt6.exe"
            if (Test-Path $TestWinDeploy) {
                $Qt6BinDir = Join-Path $Path "bin"
                $WinDeployQt = $TestWinDeploy
                $Qt6Dir = $Path
                Write-Step "Found Qt6 at: $Path"
                break
            }
        }
    }
}

if (-not $WinDeployQt) {
    Exit-WithError @"
Qt6 (windeployqt6.exe) was not found.

Please install Qt6 via the online installer or aqtinstall:
    pip install aqtinstall
    aqt install-qt windows desktop 6.8.2 win64_msvc2022_64 --outputdir C:\Qt

Or set QT6_DIR environment variable to your Qt6 installation.
"@
}

Write-Step "windeployqt6.exe: $WinDeployQt"

# =============================================================================
# Step 6: Detect Inno Setup
# =============================================================================
Write-Header "Detecting Inno Setup 6"

$ISCCPath = $null

if (-not $SkipInstaller) {
    if ($InnoSetupDir) {
        $TestISCC = Join-Path $InnoSetupDir "ISCC.exe"
        if (Test-Path $TestISCC) {
            $ISCCPath = $TestISCC
            Write-Step "Using provided Inno Setup at: $InnoSetupDir"
        }
    }

    # Check PATH
    if (-not $ISCCPath -and (Test-Command "iscc")) {
        $ISCCPath = "iscc"
        Write-Step "Found ISCC.exe in PATH"
    }

    # Check registry and common paths
    if (-not $ISCCPath) {
        $ISScriptPaths = @(
            "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
            "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
            "${env:LOCALAPPDATA}\Programs\Inno Setup 6\ISCC.exe"
        )
        foreach ($Path in $ISScriptPaths) {
            if (Test-Path $Path) {
                $ISCCPath = $Path
                Write-Step "Found Inno Setup 6 at: $Path"
                break
            }
        }
    }

    # Check Windows Registry
    if (-not $ISCCPath) {
        try {
            $RegPath = "HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Inno Setup 6_is1"
            if (Test-Path $RegPath) {
                $RegValue = Get-ItemProperty $RegPath -Name "InstallLocation" -ErrorAction SilentlyContinue
                if ($RegValue -and $RegValue.InstallLocation) {
                    $TestISCC = Join-Path $RegValue.InstallLocation "ISCC.exe"
                    if (Test-Path $TestISCC) {
                        $ISCCPath = $TestISCC
                        Write-Step "Found Inno Setup 6 via registry: $($RegValue.InstallLocation)"
                    }
                }
            }
        } catch {
            # Registry check failed, continue
        }
    }

    if (-not $ISCCPath) {
        Write-Warn "Inno Setup 6 not found. .exe installer will be skipped."
        Write-Warn "Install via: winget install JRSoftware.InnoSetup"
        $SkipInstaller = $true
    }
}

# =============================================================================
# Step 7: Detect Python
# =============================================================================
Write-Header "Detecting Python"

$PythonCmd = $null
$PythonMinVersion = [Version]::new(3, 11, 0)

$PythonCandidates = @("python", "python3", "py", "python3.12", "python3.13")
foreach ($Candidate in $PythonCandidates) {
    if (Test-Command $Candidate) {
        try {
            $PyVersion = & $Candidate -c "import sys; print(sys.version_info.major, sys.version_info.minor, sys.version_info.micro)" 2>$null
            $PyVersionParts = $PyVersion -split ' ' | ForEach-Object { [int]$_ }
            $PyVersionObj = [Version]::new($PyVersionParts[0], $PyVersionParts[1], $PyVersionParts[2])
            if ($PyVersionObj -ge $PythonMinVersion) {
                $PythonCmd = $Candidate
                Write-Step "Found Python $PyVersionObj at: $(Get-Command $Candidate | Select-Object -ExpandProperty Source)"
                break
            }
        } catch {
            continue
        }
    }
}

if (-not $PythonCmd) {
    Write-Warn "Python 3.11+ not found. Python bindings and runtime packaging will be skipped."
    Write-Warn "Install via: winget install Python.Python.3.12"
}

# =============================================================================
# Step 8: Prepare Directories
# =============================================================================
Write-Header "Preparing Build Directories"

if ($Clean -and (Test-Path $OutputDir)) {
    Write-Step "Cleaning output directory: $OutputDir"
    Remove-Item -Path $OutputDir -Recurse -Force -ErrorAction SilentlyContinue
}

$Directories = @($BuildDir, $PackageDir, $DistDir)
foreach ($Dir in $Directories) {
    if (-not (Test-Path $Dir)) {
        New-Item -ItemType Directory -Path $Dir -Force | Out-Null
        Write-Step "Created directory: $Dir"
    } else {
        Write-Step "Directory exists: $Dir"
    }
}

# =============================================================================
# Step 9: Configure CMake
# =============================================================================
if (-not $SkipBuild) {
    Write-Header "Configuring CMake ($BuildType x64)"

    $CMakeArgs = @(
        "-S", $ProjectRoot,
        "-B", $BuildDir,
        "-G", "Visual Studio 17 2022",
        "-A", "x64",
        "-DCMAKE_BUILD_TYPE=$BuildType",
        "-DCMAKE_INSTALL_PREFIX=$PackageDir"
    )

    # Add vcpkg toolchain
    if ($VcpkgRoot) {
        $VcpkgToolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
        if (Test-Path $VcpkgToolchain) {
            $CMakeArgs += "-DCMAKE_TOOLCHAIN_FILE=$VcpkgToolchain"
            Write-Step "Using vcpkg toolchain: $VcpkgToolchain"
        } else {
            Write-Warn "vcpkg.cmake toolchain not found at expected location"
        }

        # Set vcpkg triplet
        $CMakeArgs += "-DVCPKG_TARGET_TRIPLET=x64-windows"
        Write-Step "vcpkg triplet: x64-windows"
    }

    # Add Qt6 prefix path
    if ($Qt6Dir) {
        $CMakeArgs += "-DCMAKE_PREFIX_PATH=$Qt6Dir"
        Write-Step "Qt6 prefix: $Qt6Dir"
    }

    # Add build options
    $CMakeArgs += @(
        "-DBUILD_UI=ON",
        "-DBUILD_PYTHON=$($PythonCmd -ne $null ? 'ON' : 'OFF')",
        "-DBUILD_TESTS=OFF",
        "-DBUILD_SCADA=ON",
        "-DBUILD_SIMULATION=ON",
        "-DBUILD_IDE=ON",
        "-DBUILD_AI=ON",
        "-DBUILD_MODELS=ON",
        "-DBUILD_HARMONICS=ON",
        "-DBUILD_MARKETS=ON",
        "-DBUILD_RELIABILITY=ON",
        "-DBUILD_LICENSING=ON",
        "-DBUILD_INTEGRATION=ON",
        "-DBUILD_ICON_ENGINE=ON",
        "-DBUILD_I18N=ON",
        "-DBUILD_IO=ON",
        "-DBUILD_GIS=ON",
        "-DBUILD_XTALK=ON",
        "-DBUILD_LEGAL=ON",
        "-DBUILD_AUDIO=ON",
        "-DBUILD_HELP=ON",
        "-DBUILD_LINE_DESIGN=ON",
        "-DBUILD_CONFIG=ON",
        "-DBUILD_PACKAGING=ON",
        "-DENABLE_OPENMP=ON",
        "-DBUILD_SHARED_LIBS=OFF"
    )

    if ($VerboseCMake) {
        $CMakeArgs += "--debug-output"
    }

    Write-Step "CMake command: $CMakeCmd $($CMakeArgs -join ' ')"
    Write-Host ""

    & $CMakeCmd @CMakeArgs 2>&1 | ForEach-Object {
        if ($_ -match "error|ERROR|Error" -and $_ -notmatch "warning|Warning") {
            Write-Host "  $_" -ForegroundColor Red
        } elseif ($_ -match "warning|WARNING") {
            Write-Host "  $_" -ForegroundColor Yellow
        } else {
            Write-Host "  $_"
        }
    }

    if ($LASTEXITCODE -ne 0) {
        Exit-WithError "CMake configuration failed. Check the output above for errors."
    }
    Write-Step "CMake configuration completed successfully"

    # =============================================================================
    # Step 10: Build All Modules
    # =============================================================================
    Write-Header "Building All 23 Modules ($BuildType)"

    $BuildArgs = @(
        "--build", $BuildDir,
        "--config", $BuildType,
        "--parallel"
    )

    # Get number of logical processors for parallel build
    $ProcessorCount = [Environment]::ProcessorCount
    Write-Step "Using $ProcessorCount parallel build jobs"

    Write-Step "Build command: $CMakeCmd $($BuildArgs -join ' ')"
    Write-Host ""

    & $CMakeCmd @BuildArgs 2>&1 | ForEach-Object {
        if ($_ -match "error [A-Z]*[0-9]+:" -or $_ -match "fatal error") {
            Write-Host "  $_" -ForegroundColor Red
        } elseif ($_ -match "warning [A-Z]*[0-9]+:") {
            Write-Host "  $_" -ForegroundColor Yellow
        } elseif ($_ -match "Building|Linking|Generating|Created") {
            Write-Host "  $_" -ForegroundColor Green
        } else {
            Write-Host "  $_"
        }
    }

    if ($LASTEXITCODE -ne 0) {
        Exit-WithError "Build failed. Check the output above for compilation errors."
    }
    Write-Step "Build completed successfully"

    # =============================================================================
    # Step 11: Install to Package Directory
    # =============================================================================
    Write-Header "Installing to Package Directory"

    $InstallArgs = @(
        "--install", $BuildDir,
        "--prefix", $PackageDir,
        "--config", $BuildType
    )

    & $CMakeCmd @InstallArgs 2>&1 | ForEach-Object {
        Write-Host "  $_"
    }

    if ($LASTEXITCODE -ne 0) {
        Write-Warn "CMake install failed, continuing with manual packaging..."
    } else {
        Write-Step "CMake install completed"
    }
}

# =============================================================================
# Step 12: Manual Assembly of Package Directory
# =============================================================================
Write-Header "Assembling Package Contents"

# Define package structure
$PkgBinDir = Join-Path $PackageDir "bin"
$PkgLibDir = Join-Path $PackageDir "lib"
$PkgShareDir = Join-Path $PackageDir "share\POWSYS365"
$PkgPythonDir = Join-Path $PackageDir "python"
$PkgResourcesDir = Join-Path $PackageDir "resources"
$PkgI18nDir = Join-Path $PackageDir "i18n"
$PkgHelpDir = Join-Path $PackageDir "help"
$PkgDbDir = Join-Path $PackageDir "database"
$PkgAiDir = Join-Path $PackageDir "ai"
$PkgDocDir = Join-Path $PackageDir "docs"
$PkgLicenseDir = Join-Path $PackageDir "legal"

$AllPkgDirs = @($PkgBinDir, $PkgLibDir, $PkgShareDir, $PkgPythonDir, $PkgResourcesDir,
                $PkgI18nDir, $PkgHelpDir, $PkgDbDir, $PkgAiDir, $PkgDocDir, $PkgLicenseDir)
foreach ($Dir in $AllPkgDirs) {
    if (-not (Test-Path $Dir)) {
        New-Item -ItemType Directory -Path $Dir -Force | Out-Null
    }
}

# --- 12a: Copy executable ---
Write-Step "Copying executables..."
$ExeSource = Join-Path $BuildDir "$BuildType\POWSYS365.exe"
if (-not (Test-Path $ExeSource)) {
    # Try alternate paths
    $AltPaths = @(
        (Join-Path $BuildDir "bin\$BuildType\POWSYS365.exe"),
        (Join-Path $BuildDir "POWSYS365.exe"),
        (Join-Path $BuildDir "ui\$BuildType\POWSYS365.exe"),
        (Join-Path $BuildDir "bin\POWSYS365.exe")
    )
    foreach ($Alt in $AltPaths) {
        if (Test-Path $Alt) {
            $ExeSource = $Alt
            break
        }
    }
}

if (Test-Path $ExeSource) {
    Copy-Item -Path $ExeSource -Destination $PkgBinDir -Force
    Write-Step "Copied: POWSYS365.exe"
} else {
    Write-Warn "POWSYS365.exe not found. Searched:"
    Write-Warn "  $ExeSource"
    foreach ($Alt in $AltPaths) { Write-Warn "  $Alt" }
}

# Copy any other executables (tools, test utils)
$ExtraExes = Get-ChildItem -Path $BuildDir -Recurse -Filter "*.exe" -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -ne "POWSYS365.exe" -and $_.Name -notmatch "test" -and $_.Name -notmatch "Test" } |
    Select-Object -ExpandProperty FullName
foreach ($Exe in $ExtraExes) {
    Copy-Item -Path $Exe -Destination $PkgBinDir -Force
    Write-Step "Copied tool: $(Split-Path $Exe -Leaf)"
}

# --- 12b: Copy DLLs (non-Qt) ---
Write-Step "Copying library DLLs..."
$DllPatterns = @("*.dll")
$DllExcludePatterns = @("Qt6*", "qwindows*", "qminimal*", "qdirect2d*", "qgeneric*")

$AllDlls = Get-ChildItem -Path (Join-Path $BuildDir $BuildType) -Filter "*.dll" -ErrorAction SilentlyContinue
if (-not $AllDlls) {
    $AllDlls = Get-ChildItem -Path $BuildDir -Recurse -Filter "*.dll" -ErrorAction SilentlyContinue |
        Where-Object {
            $Name = $_.Name
            foreach ($Pat in $DllExcludePatterns) {
                if ($Name -like $Pat) { return $false }
            }
            return $true
        }
}
foreach ($Dll in $AllDlls) {
    Copy-Item -Path $Dll.FullName -Destination $PkgBinDir -Force
    Write-Step "  DLL: $($Dll.Name)"
}

# --- 12c: Deploy Qt6 with windeployqt6 ---
Write-Step "Deploying Qt6 runtime with windeployqt6..."
$WdqArgs = @(
    "--release",
    "--dir", $PkgBinDir,
    "--plugindir", (Join-Path $PkgBinDir "plugins"),
    "--qmlplugindir", (Join-Path $PkgBinDir "qml"),
    "--qmldir", (Join-Path $ProjectRoot "ui\qml"),
    "--no-translations",         # We handle translations manually
    "--no-system-d3d-compiler",  # Skip D3D compiler to reduce size
    "--no-virtualkeyboard",      # Skip virtual keyboard
    "--compiler-runtime",        # Include MSVC runtime
    "--force",
    (Join-Path $PkgBinDir "POWSYS365.exe")
)

& $WinDeployQt @WdqArgs 2>&1 | ForEach-Object {
    if ($_ -match "error|ERROR|cannot find|does not exist" -and $_ -notmatch "Warning") {
        Write-Host "  $_" -ForegroundColor Red
    } elseif ($_ -match "Warning|warning") {
        Write-Host "  $_" -ForegroundColor Yellow
    } else {
        Write-Host "  $_" -ForegroundColor Gray
    }
}

# Check if windeployqt succeeded
$Qt6CoreDll = Join-Path $PkgBinDir "Qt6Core.dll"
if (Test-Path $Qt6CoreDll) {
    Write-Step "Qt6 deployment completed successfully"
} else {
    Write-Warn "Qt6 deployment may have issues. Qt6Core.dll not found in output."
}

# --- 12d: Copy Resources ---
Write-Step "Copying resources..."
$ResourceItems = @("icon.svg", "splash.png", "banner.png", "*.qss", "*.css", "themes")
foreach ($Item in $ResourceItems) {
    $SourcePath = Join-Path $ProjectRoot "resources" $Item
    if (Test-Path $SourcePath) {
        if ((Get-Item $SourcePath).PSIsContainer) {
            Copy-Item -Path $SourcePath -Destination $PkgResourcesDir -Recurse -Force
        } else {
            Copy-Item -Path $SourcePath -Destination $PkgResourcesDir -Force
        }
        Write-Step "  Resource: $Item"
    }
}

# Convert SVG icon to ICO for Windows if needed
$IconSvg = Join-Path $ProjectRoot "resources" "icon.svg"
$IconIco = Join-Path $PkgResourcesDir "icon.ico"
if (Test-Path $IconSvg) {
    Copy-Item -Path $IconSvg -Destination $PkgResourcesDir -Force
    Write-Step "  Copied icon.svg"
    # Try to create .ico from .svg using ImageMagick or inkscape
    if (Test-Command "magick") {
        & magick convert -background none $IconSvg -define icon:auto-resize=256,128,64,48,32,16 $IconIco 2>$null
        if (Test-Path $IconIco) {
            Write-Step "  Generated icon.ico from SVG"
        }
    } elseif (Test-Command "inkscape") {
        & inkscape $IconSvg --export-filename=$IconIco --export-width=256 --export-height=256 2>$null
        if (Test-Path $IconIco) {
            Write-Step "  Generated icon.ico from SVG"
        }
    }
}

# --- 12e: Copy i18n / Translations ---
Write-Step "Copying internationalization files..."
$TsFiles = Get-ChildItem -Path (Join-Path $ProjectRoot "i18n") -Filter "*.ts" -ErrorAction SilentlyContinue
if ($TsFiles) {
    # Compile .ts to .qm using lrelease
    $LRelease = Join-Path $Qt6BinDir "lrelease.exe"
    if (Test-Path $LRelease) {
        foreach ($Ts in $TsFiles) {
            $QmFile = Join-Path $PkgI18nDir ($Ts.BaseName + ".qm")
            & $LRelease $Ts.FullName -qm $QmFile 2>&1 | Out-Null
            if (Test-Path $QmFile) {
                Write-Step "  Compiled: $($Ts.BaseName).qm"
            } else {
                # Just copy .ts if lrelease fails
                Copy-Item -Path $Ts.FullName -Destination $PkgI18nDir -Force
                Write-Step "  Copied TS: $($Ts.Name)"
            }
        }
    } else {
        foreach ($Ts in $TsFiles) {
            Copy-Item -Path $Ts.FullName -Destination $PkgI18nDir -Force
            Write-Step "  Copied TS: $($Ts.Name)"
        }
    }
}

# Copy TranslationManager sources
$I18nSources = Get-ChildItem -Path (Join-Path $ProjectRoot "i18n") -Filter "*.cpp" -ErrorAction SilentlyContinue
foreach ($Src in $I18nSources) {
    Copy-Item -Path $Src.FullName -Destination $PkgI18nDir -Force
}

# --- 12f: Copy Help Files ---
Write-Step "Copying help/documentation files..."
$HelpFiles = Get-ChildItem -Path (Join-Path $ProjectRoot "help") -File -ErrorAction SilentlyContinue
foreach ($File in $HelpFiles) {
    Copy-Item -Path $File.FullName -Destination $PkgHelpDir -Force
    Write-Step "  Help: $($File.Name)"
}

# --- 12g: Copy Database Schema ---
Write-Step "Copying database files..."
$DbSubDirs = @("migrations", "queries", "seeds")
foreach ($SubDir in $DbSubDirs) {
    $Src = Join-Path $ProjectRoot "database" $SubDir
    $Dst = Join-Path $PkgDbDir $SubDir
    if (Test-Path $Src) {
        New-Item -ItemType Directory -Path $Dst -Force | Out-Null
        Copy-Item -Path "$Src\*" -Destination $Dst -Recurse -Force
        Write-Step "  DB $SubDir: copied"
    }
}
# Copy schema.sql
$SchemaSql = Join-Path $ProjectRoot "database" "schema.sql"
if (Test-Path $SchemaSql) {
    Copy-Item -Path $SchemaSql -Destination $PkgDbDir -Force
    Write-Step "  DB schema.sql: copied"
}

# --- 12h: Copy AI Prompts & Scripts ---
Write-Step "Copying AI integration files..."
$AiPythonDir = Join-Path $ProjectRoot "ai\python"
if (Test-Path $AiPythonDir) {
    $AiPythonDst = Join-Path $PkgAiDir "python"
    New-Item -ItemType Directory -Path $AiPythonDst -Force | Out-Null
    Copy-Item -Path "$AiPythonDir\*" -Destination $AiPythonDst -Recurse -Force
    Write-Step "  AI python scripts: copied"
}

# --- 12i: Copy Python Package ---
if ($PythonCmd) {
    Write-Step "Copying Python package..."
    $PyPkgSrc = Join-Path $ProjectRoot "python\powsy365"
    if (Test-Path $PyPkgSrc) {
        $PyPkgDst = Join-Path $PkgPythonDir "powsy365"
        New-Item -ItemType Directory -Path $PyPkgDst -Force | Out-Null
        Copy-Item -Path "$PyPkgSrc\*" -Destination $PyPkgDst -Recurse -Force
        Write-Step "  Python package: powsy365/"
    }
    # Copy setup.py and requirements.txt
    $PySetup = Join-Path $ProjectRoot "python\setup.py"
    $PyReq = Join-Path $ProjectRoot "python\requirements.txt"
    if (Test-Path $PySetup) { Copy-Item -Path $PySetup -Destination $PkgPythonDir -Force }
    if (Test-Path $PyReq) { Copy-Item -Path $PyReq -Destination $PkgPythonDir -Force }

    # Install Python dependencies into the package
    Write-Step "Installing Python dependencies into package..."
    $PySitePackages = Join-Path $PkgPythonDir "site-packages"
    if (Test-Path $PyReq) {
        & $PythonCmd -m pip install -r $PyReq --target $PySitePackages --quiet 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Write-Step "  Python deps installed to site-packages/"
        } else {
            Write-Warn "  Failed to install Python dependencies"
        }
    }
}

# --- 12j: Copy Documentation ---
Write-Step "Copying documentation..."
$DocFiles = @("README.md", "CHANGELOG.md", "LICENSE", "AUTHORS.md")
foreach ($Doc in $DocFiles) {
    $DocPath = Join-Path $ProjectRoot $Doc
    if (Test-Path $DocPath) {
        Copy-Item -Path $DocPath -Destination $PkgDocDir -Force
        Write-Step "  Doc: $Doc"
    }
}

# Copy docs/ directory
$DocsDir = Join-Path $ProjectRoot "docs"
if (Test-Path $DocsDir) {
    Copy-Item -Path "$DocsDir\*" -Destination $PkgDocDir -Recurse -Force
    Write-Step "  docs/: copied"
}

# --- 12k: Copy Legal Files ---
Write-Step "Copying legal files..."
$LegalDir = Join-Path $ProjectRoot "legal"
if (Test-Path $LegalDir) {
    Copy-Item -Path "$LegalDir\*" -Destination $PkgLicenseDir -Recurse -Force
    Write-Step "  legal/: copied"
} else {
    # Copy LICENSE to root of package
    $LicenseFile = Join-Path $ProjectRoot "LICENSE"
    if (Test-Path $LicenseFile) {
        Copy-Item -Path $LicenseFile -Destination $PackageDir -Force
        Write-Step "  LICENSE: copied to package root"
    }
}

# --- 12l: Copy Config Templates ---
Write-Step "Copying configuration templates..."
$ConfigDir = Join-Path $ProjectRoot "config"
if (Test-Path $ConfigDir) {
    $PkgConfigDir = Join-Path $PackageDir "config"
    New-Item -ItemType Directory -Path $PkgConfigDir -Force | Out-Null
    Copy-Item -Path "$ConfigDir\*.json" -Destination $PkgConfigDir -Force -ErrorAction SilentlyContinue
    Copy-Item -Path "$ConfigDir\*.yaml" -Destination $PkgConfigDir -Force -ErrorAction SilentlyContinue
    Copy-Item -Path "$ConfigDir\*.yml" -Destination $PkgConfigDir -Force -ErrorAction SilentlyContinue
    Write-Step "  Config templates copied"
}

# --- 12m: Create .qmldir reference for QML deployment ---
Write-Step "Setting up QML deployment..."
$QmlSourceDir = Join-Path $ProjectRoot "ui\qml"
if (Test-Path $QmlSourceDir) {
    $QmlDeployDir = Join-Path $PkgBinDir "qml"
    if (-not (Test-Path $QmlDeployDir)) {
        New-Item -ItemType Directory -Path $QmlDeployDir -Force | Out-Null
    }
    # Copy QML files for runtime reference
    Copy-Item -Path "$QmlSourceDir\*.qml" -Destination $QmlDeployDir -Force
    Copy-Item -Path "$QmlSourceDir\qmldir" -Destination $QmlDeployDir -Force
    Write-Step "  QML files deployed"
}

# =============================================================================
# Step 13: Verify DLL Dependencies
# =============================================================================
Write-Header "Verifying DLL Dependencies"

# List of known required DLLs to check
$RequiredDlls = @(
    "Qt6Core.dll",
    "Qt6Gui.dll",
    "Qt6Widgets.dll",
    "Qt6Quick.dll",
    "Qt6QuickControls2.dll",
    "Qt6Network.dll",
    "Qt6Sql.dll",
    "Qt6Charts.dll"
)

foreach ($Dll in $RequiredDlls) {
    $DllPath = Join-Path $PkgBinDir $Dll
    if (Test-Path $DllPath) {
        Write-Step "  OK: $Dll"
    } else {
        Write-Warn "  MISSING: $Dll"
    }
}

# Check MSVC runtime
$MsvcDlls = @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll")
foreach ($Dll in $MsvcDlls) {
    $DllPath = Join-Path $PkgBinDir $Dll
    if (Test-Path $DllPath) {
        Write-Step "  OK: $Dll (MSVC runtime)"
    } else {
        Write-Warn "  MISSING: $Dll (MSVC runtime)"
    }
}

# =============================================================================
# Step 14: Generate Inno Setup Installer
# =============================================================================
if (-not $SkipInstaller -and $ISCCPath) {
    Write-Header "Generating Inno Setup Installer"

    $IssTemplate = Join-Path $ScriptDir "POWSYS365.iss"
    if (-not (Test-Path $IssTemplate)) {
        Write-Warn "POWSYS365.iss template not found at: $IssTemplate"
        Write-Warn "Skipping installer generation."
        Write-Warn "Expected the .iss file to be in the same directory as this script."
    } else {
        # Create a temporary copy of the .iss with paths substituted
        $IssTemp = Join-Path $OutputDir "POWSYS365_temp.iss"
        $IssContent = Get-Content $IssTemplate -Raw

        # Replace placeholder variables
        $IssContent = $IssContent -replace '#define SourceDir ".+?"', "#define SourceDir `"$PackageDir`""
        $IssContent = $IssContent -replace '#define ProjectRoot ".+?"', "#define ProjectRoot `"$ProjectRoot`""
        $IssContent = $IssContent -replace '#define OutputDir ".+?"', "#define OutputDir `"$DistDir`""
        $IssContent = $IssContent -replace '#define BuildType ".+?"', "#define BuildType `"$BuildType`""
        $IssContent = $IssContent -replace '#define VcvarsPath ".+?"', "#define VcvarsPath `"$VcvarsPath`""

        # Write the processed .iss
        $IssContent | Set-Content $IssTemp -Encoding UTF8

        Write-Step "Generated installer script: $IssTemp"
        Write-Step "Compiling with Inno Setup..."

        & $ISCCPath "`"$IssTemp`"" 2>&1 | ForEach-Object {
            if ($_ -match "error|Error") {
                Write-Host "  $_" -ForegroundColor Red
            } elseif ($_ -match "warning|Warning") {
                Write-Host "  $_" -ForegroundColor Yellow
            } else {
                Write-Host "  $_"
            }
        }

        if ($LASTEXITCODE -eq 0) {
            $InstallerFile = Get-ChildItem -Path $DistDir -Filter "POWSYS365-*-Setup.exe" -ErrorAction SilentlyContinue |
                Select-Object -ExpandProperty FullName -First 1
            if (-not $InstallerFile) {
                $InstallerFile = Get-ChildItem -Path $DistDir -Filter "*.exe" -ErrorAction SilentlyContinue |
                    Select-Object -ExpandProperty FullName -First 1
            }
            if ($InstallerFile) {
                $InstallerSize = (Get-Item $InstallerFile).Length / 1MB
                Write-Step "Installer created successfully!"
                Write-Step "  File: $InstallerFile"
                Write-Step "  Size: $([math]::Round($InstallerSize, 2)) MB"
            } else {
                Write-Step "Installer compiled (check $DistDir for output)"
            }
        } else {
            Write-Warn "Inno Setup compilation failed with exit code $LASTEXITCODE"
        }

        # Clean up temp .iss
        Remove-Item $IssTemp -Force -ErrorAction SilentlyContinue
    }
}

# =============================================================================
# Step 15: Create Portable ZIP Archive
# =============================================================================
if (-not $SkipZip) {
    Write-Header "Creating Portable ZIP Archive"

    $ZipFileName = "POWSYS365-${ProductVersion}-windows-${Arch}-portable.zip"
    $ZipFilePath = Join-Path $DistDir $ZipFileName

    # Remove existing zip
    if (Test-Path $ZipFilePath) {
        Remove-Item $ZipFilePath -Force
        Write-Step "Removed existing ZIP: $ZipFileName"
    }

    # Use Compress-Archive (PowerShell 5+)
    try {
        Compress-Archive -Path "$PackageDir\*" -DestinationPath $ZipFilePath -CompressionLevel Optimal -Force
        $ZipSize = (Get-Item $ZipFilePath).Length / 1MB
        Write-Step "ZIP created: $ZipFileName"
        Write-Step "  Path: $ZipFilePath"
        Write-Step "  Size: $([math]::Round($ZipSize, 2)) MB"
    } catch {
        Write-Warn "Failed to create ZIP with Compress-Archive: $_"

        # Fallback: Use 7-Zip if available
        if (Test-Command "7z") {
            & 7z a -tzip -mx=9 "`"$ZipFilePath`"" "`"$PackageDir\*`"" 2>&1 | Out-Null
            if (Test-Path $ZipFilePath) {
                $ZipSize = (Get-Item $ZipFilePath).Length / 1MB
                Write-Step "ZIP created with 7-Zip: $ZipFileName ($([math]::Round($ZipSize, 2)) MB)"
            }
        } else {
            Write-Warn "ZIP creation failed. Install 7-Zip or use PowerShell 5+."
        }
    }
}

# =============================================================================
# Step 16: Generate Build Report
# =============================================================================
Write-Header "Build Summary Report"

$PackageSize = "0"
$PkgItemCount = 0
if (Test-Path $PackageDir) {
    $PkgItems = Get-ChildItem -Path $PackageDir -Recurse -File -ErrorAction SilentlyContinue
    $PkgItemCount = $PkgItems.Count
    $TotalSize = ($PkgItems | Measure-Object -Property Length -Sum).Sum
    $PackageSize = "{0:N2}" -f ($TotalSize / 1MB)
}

$Report = @"
================================================================
              POWSYS365 Build Report
================================================================
  Product:      $ProductName v$ProductVersion
  Publisher:    $Publisher
  Architecture: x64 (Windows)
  Build Type:   $BuildType
  Platform:     Windows $([Environment]::OSVersion.Version)

  Directories:
    Build:      $BuildDir
    Package:    $PackageDir
    Output:     $DistDir

  Environment:
    Visual Studio:  $VsInstallPath
    CMake:          $CMakeCmd
    vcpkg:          $(if ($VcpkgRoot) { $VcpkgRoot } else { "NOT FOUND" })
    Qt6:            $Qt6Dir
    Inno Setup:     $(if ($ISCCPath) { $ISCCPath } else { "NOT FOUND" })
    Python:         $(if ($PythonCmd) { $(Get-Command $PythonCmd | Select-Object -ExpandProperty Source) } else { "NOT FOUND" })

  Package Contents:
    Files:      $PkgItemCount items
    Total Size: $PackageSize MB

  Generated Artifacts:
"@

Write-Host $Report

# List artifacts
$Artifacts = Get-ChildItem -Path $DistDir -File -ErrorAction SilentlyContinue
if ($Artifacts) {
    foreach ($Art in $Artifacts) {
        $ArtSize = "{0:N2}" -f ($Art.Length / 1MB)
        Write-Host "    - $($Art.Name) ($ArtSize MB)" -ForegroundColor Green
    }
} else {
    Write-Host "    (No artifacts generated)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host "  Build completed at: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')" -ForegroundColor Cyan
Write-Host "================================================================" -ForegroundColor Cyan
Write-Host ""
