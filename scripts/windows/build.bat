@echo off
REM =============================================================================
REM POWSYS365 - Windows Batch Build Script
REM =============================================================================
REM Script Name : build.bat
REM Version     : 3.0.0
REM Author      : XNOX L.L.C.
REM Description : Standalone batch build script for POWSYS365 on Windows.
REM               No PowerShell required. Configures MSVC environment,
REM               builds with CMake + Ninja, deploys Qt6 DLLs, and creates
REM               a portable ZIP archive.
REM
REM Usage:
REM   build.bat                            - Default Release build
REM   build.bat debug                      - Debug build
REM   build.bat clean                      - Clean build directories
REM   build.bat relwithdebinfo             - RelWithDebInfo build
REM   build.bat --help                     - Show help
REM
REM Prerequisites:
REM   - Visual Studio 2022 or Build Tools 2022
REM   - CMake 3.31+ (in PATH)
REM   - vcpkg (VCPKG_ROOT env var set)
REM   - Qt6 6.5+ (QT6_DIR env var set)
REM   - Ninja build system (bundled with VS or in PATH)
REM   - windeployqt6.exe (part of Qt6)
REM   - Python 3.11+ (optional, for Python bindings)
REM =============================================================================

setlocal EnableDelayedExpansion

REM ---------------------------------------------------------------------------
REM Configuration
REM ---------------------------------------------------------------------------
set "SCRIPT_VERSION=3.0.0"
set "PRODUCT_NAME=POWSYS365"
set "PRODUCT_VERSION=3.0.0"
set "PUBLISHER=XNOX L.L.C."
set "ARCH=x64"

REM Determine script and project directories
set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "PROJECT_ROOT=%%~fI"
for %%I in ("%SCRIPT_DIR%..") do for %%J in ("%%~fI..") do set "PROJECT_ROOT=%%~fJ"

REM Default build directories
set "BUILD_DIR=%PROJECT_ROOT%\build-windows\build"
set "PACKAGE_DIR=%PROJECT_ROOT%\build-windows\package"
set "DIST_DIR=%PROJECT_ROOT%\build-windows\dist"

REM Parse command line arguments
set "BUILD_TYPE=Release"
set "CLEAN_BUILD=0"
set "SKIP_BUILD=0"
set "SKIP_QT=0"
set "SKIP_ZIP=0"

if /I "%~1"=="debug" (
    set "BUILD_TYPE=Debug"
) else if /I "%~1"=="clean" (
    set "CLEAN_BUILD=1"
) else if /I "%~1"=="relwithdebinfo" (
    set "BUILD_TYPE=RelWithDebInfo"
) else if /I "%~1"=="--help" (
    goto :ShowHelp
) else if /I "%~1"=="/help" (
    goto :ShowHelp
) else if /I "%~1"=="/?" (
    goto :ShowHelp
) else if "%~1"=="" (
    REM Default: Release build
) else (
    echo [WARNING] Unknown argument: %~1
    echo           Using default: Release
    echo           Use --help for usage information
    echo.
    timeout /t 2 /nobreak >nul
)

REM ---------------------------------------------------------------------------
REM Banner
REM ---------------------------------------------------------------------------
echo.
echo     ____  ____  _    __ ____   ___  ____  _  ___
echo    / __ \/ __ \^| ^|  / // __ \ /   ^|/ __ \/ ^|/ / /
echo   / /_/ / / / /^| ^| / // /_/ // /^| // / / /   / /
echo  / ____/ /_/ / ^| ^|/ // _, _// ___ ^|/ /_/ /   ^| ^|
echo /_/    \____/  ^|___//_/ ^|_/_/  ^|_/\____/_/^|_^_^|_
echo.
echo   Windows Batch Build Script v%SCRIPT_VERSION%
echo   (c) 2025 %PUBLISHER%
echo.

REM ---------------------------------------------------------------------------
REM Step 1: Validate Windows Version (Windows 10 1809+)
REM ---------------------------------------------------------------------------
echo [Step 1/12] Validating Windows version...

for /f "tokens=4-5 delims=. " %%a in ('ver') do (
    set "WIN_MAJOR=%%a"
    set "WIN_MINOR=%%b"
)

REM Extract just the major version number
for /f "tokens=1 delims=." %%v in ("%WIN_MAJOR%") do set "WIN_MAJOR=%%v"

if %WIN_MAJOR% LSS 10 (
    echo [ERROR] Windows 10 or later is required.
    echo         Current version detected: %WIN_MAJOR%.%WIN_MINOR%
    exit /b 1
)

echo   [OK] Windows %WIN_MAJOR%.%WIN_MINOR% detected

REM ---------------------------------------------------------------------------
REM Step 2: Check 64-bit architecture
REM ---------------------------------------------------------------------------
echo.
echo [Step 2/12] Checking architecture...
if "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
    echo   [OK] x64 architecture detected
) else (
    echo [ERROR] x64 (64-bit) architecture is required.
    echo         Current: %PROCESSOR_ARCHITECTURE%
    exit /b 1
)

REM ---------------------------------------------------------------------------
REM Step 3: Detect Visual Studio 2022
REM ---------------------------------------------------------------------------
echo.
echo [Step 3/12] Detecting Visual Studio 2022...

set "VSWHERE_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE_PATH%" (
    set "VSWHERE_PATH=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"
)

set "VS_INSTALL_PATH="
set "VCVARS_PATH="

REM Try vswhere.exe first
if exist "%VSWHERE_PATH%" (
    echo   Found vswhere.exe
    for /f "delims=" %%i in ('"%VSWHERE_PATH%" -version "[17.0,18.0)" -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath -latest 2^>nul') do (
        set "VS_INSTALL_PATH=%%i"
    )
)

REM Fallback: search common paths
if not defined VS_INSTALL_PATH (
    echo   Searching common installation paths...
    set "VS_PATHS=^\n^"%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise^"\n^"%ProgramFiles%\Microsoft Visual Studio\2022\Professional^"\n^"%ProgramFiles%\Microsoft Visual Studio\2022\Community^"\n^"%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools^"\n^"%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise^"\n^"%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional^"\n^"%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community^"\n^"%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools^""
    
    for /d %%p in (
        "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Professional"
        "%ProgramFiles%\Microsoft Visual Studio\2022\Community"
        "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community"
        "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools"
    ) do (
        if exist "%%p\VC\Auxiliary\Build\vcvars64.bat" (
            set "VS_INSTALL_PATH=%%p"
            echo   [OK] Found VS2022 at: %%p
            goto :FoundVS
        )
    )
    echo [ERROR] Visual Studio 2022 not found.
    echo.
    echo Please install one of the following:
    echo   - Visual Studio 2022 Community/Professional/Enterprise
    echo   - Visual Studio Build Tools 2022
    echo.
    echo Required C++ workload:
    echo   - MSVC v143 - VS 2022 C++ x64/x86 build tools
    echo   - Windows SDK
    echo.
    echo Download: https://visualstudio.microsoft.com/downloads/
    exit /b 1
)

:FoundVS
echo   [OK] Visual Studio 2022: %VS_INSTALL_PATH%

REM Set vcvars64.bat path
set "VCVARS_PATH=%VS_INSTALL_PATH%\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS_PATH%" (
    set "VCVARS_PATH=%VS_INSTALL_PATH%\VC\Auxiliary\Build\vcvarsall.bat"
    if not exist "%VCVARS_PATH%" (
        echo [ERROR] vcvars script not found in Visual Studio installation.
        exit /b 1
    )
    echo   Using vcvarsall.bat with x64 argument
)

REM ---------------------------------------------------------------------------
REM Step 4: Detect CMake
REM ---------------------------------------------------------------------------
echo.
echo [Step 4/12] Detecting CMake 3.31+...

cmake --version >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    for /f "tokens=3" %%v in ('cmake --version 2^>^&1 ^| findstr /C:"cmake version"') do (
        set "CMAKE_VERSION=%%v"
    )
    echo   [OK] CMake %CMAKE_VERSION% found in PATH
    set "CMAKE_CMD=cmake"
    goto :CMakeFound
)

REM Check common CMake paths
set "CMAKE_PATHS=^\n^"%ProgramFiles%\CMake\bin\cmake.exe^"\n^"%ProgramFiles(x86)%\CMake\bin\cmake.exe^"\n^"%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe^""
for %%p in (
    "%ProgramFiles%\CMake\bin\cmake.exe"
    "%ProgramFiles(x86)%\CMake\bin\cmake.exe"
    "%LOCALAPPDATA%\Programs\CMake\bin\cmake.exe"
    "%LOCALAPPDATA%\Microsoft\WinGet\Packages\Kitware.CMake_Microsoft.Winget.Source_8wekyb3d8bbwe\bin\cmake.exe"
) do (
    if exist "%%p" (
        set "CMAKE_CMD=%%p"
        for /f "tokens=3" %%v in ('"%%p" --version 2^>^&1 ^| findstr /C:"cmake version"') do (
            set "CMAKE_VERSION=%%v"
        )
        echo   [OK] CMake %CMAKE_VERSION% found at: %%p
        goto :CMakeFound
    )
)

echo [ERROR] CMake 3.31+ not found.
echo.
echo Install via winget:
echo     winget install Kitware.CMake
echo.
echo Or download from: https://cmake.org/download/
exit /b 1

:CMakeFound

REM ---------------------------------------------------------------------------
REM Step 5: Detect vcpkg
REM ---------------------------------------------------------------------------
echo.
echo [Step 5/12] Detecting vcpkg...

set "VCPKG_EXE="
set "VCPKG_TOOLCHAIN="

REM Check VCPKG_ROOT environment variable
if defined VCPKG_ROOT (
    if exist "%VCPKG_ROOT%\vcpkg.exe" (
        set "VCPKG_EXE=%VCPKG_ROOT%\vcpkg.exe"
        echo   [OK] vcpkg found via VCPKG_ROOT: %VCPKG_ROOT%
        goto :VcpkgFound
    )
)

REM Check common locations
for %%p in (
    "C:\vcpkg\vcpkg.exe"
    "%LOCALAPPDATA%\vcpkg\vcpkg.exe"
    "%USERPROFILE%\vcpkg\vcpkg.exe"
    "%ProgramFiles%\vcpkg\vcpkg.exe"
    "%DEVTOOLS%\vcpkg\vcpkg.exe"
) do (
    if exist "%%p" (
        set "VCPKG_EXE=%%p"
        for %%d in ("%%p") do set "VCPKG_ROOT=%%~pd"
        set "VCPKG_ROOT=!VCPKG_ROOT:~0,-1!"
        echo   [OK] vcpkg found at: !VCPKG_ROOT!
        goto :VcpkgFound
    )
)

echo   [WARN] vcpkg not found. Some dependencies may be fetched via CMake.
echo          Set VCPKG_ROOT environment variable if you have vcpkg installed.
echo          Continuing with CMake FetchContent fallback...

:VcpkgFound
if defined VCPKG_ROOT (
    set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
    if exist "%VCPKG_TOOLCHAIN%" (
        echo   [OK] vcpkg.cmake toolchain found
    ) else (
        echo   [WARN] vcpkg.cmake toolchain not found at expected path
        echo          %VCPKG_TOOLCHAIN%
        set "VCPKG_TOOLCHAIN="
    )
)

REM Check vcpkg.json exists
if exist "%PROJECT_ROOT%\vcpkg.json" (
    echo   [OK] vcpkg.json manifest found
) else (
    echo   [WARN] vcpkg.json not found in project root
)

REM ---------------------------------------------------------------------------
REM Step 6: Detect Qt6 and windeployqt6
REM ---------------------------------------------------------------------------
echo.
echo [Step 6/12] Detecting Qt6...

set "WINDEPLOYQT="
set "QT6_BIN_DIR="

REM Check QT6_DIR / QTDIR environment variables
if defined QT6_DIR (
    if exist "%QT6_DIR%\bin\windeployqt6.exe" (
        set "WINDEPLOYQT=%QT6_DIR%\bin\windeployqt6.exe"
        set "QT6_BIN_DIR=%QT6_DIR%\bin"
        echo   [OK] Qt6 found via QT6_DIR: %QT6_DIR%
        goto :Qt6Found
    )
)

if defined QTDIR (
    if exist "%QTDIR%\bin\windeployqt6.exe" (
        set "WINDEPLOYQT=%QTDIR%\bin\windeployqt6.exe"
        set "QT6_BIN_DIR=%QTDIR%\bin"
        echo   [OK] Qt6 found via QTDIR: %QTDIR%
        goto :Qt6Found
    )
)

REM Search common Qt6 installation paths
set "QT_VERSIONS=6.8.2 6.8.1 6.8.0 6.7.3 6.7.2 6.6.3 6.6.2 6.5.3 6.5.2 6.5.1"
set "QT_BASE_PATHS=%USERPROFILE%\Qt C:\Qt"

for %%b in (%QT_BASE_PATHS%) do (
    for %%v in (%QT_VERSIONS%) do (
        if exist "%%b\%%v\msvc2022_64\bin\windeployqt6.exe" (
            set "WINDEPLOYQT=%%b\%%v\msvc2022_64\bin\windeployqt6.exe"
            set "QT6_BIN_DIR=%%b\%%v\msvc2022_64\bin"
            set "QT6_DIR=%%b\%%v\msvc2022_64"
            echo   [OK] Qt6 %%v found at: %%b\%%v\msvc2022_64
            goto :Qt6Found
        )
    )
)

echo [ERROR] Qt6 (windeployqt6.exe) not found.
echo.
echo Please install Qt6 via the online installer or aqtinstall:
echo     pip install aqtinstall
echo     aqt install-qt windows desktop 6.8.2 win64_msvc2022_64 --outputdir C:\Qt
echo.
echo Or set QT6_DIR environment variable to your Qt6 installation.
exit /b 1

:Qt6Found
echo   windeployqt6.exe: %WINDEPLOYQT%

REM ---------------------------------------------------------------------------
REM Step 7: Detect Python (optional)
REM ---------------------------------------------------------------------------
echo.
echo [Step 7/12] Detecting Python (optional)...

set "PYTHON_CMD="
set "PYTHON_FOUND=0"

REM Check python command
python --version >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    for /f "tokens=2" %%v in ('python --version 2^>^&1') do (
        echo   [OK] Python %%v found
    )
    set "PYTHON_CMD=python"
    set "PYTHON_FOUND=1"
    goto :PythonFound
)

REM Check python3
python3 --version >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    for /f "tokens=2" %%v in ('python3 --version 2^>^&1') do (
        echo   [OK] Python %%v found
    )
    set "PYTHON_CMD=python3"
    set "PYTHON_FOUND=1"
    goto :PythonFound
)

REM Check py
py --version >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    for /f "tokens=2" %%v in ('py --version 2^>^&1') do (
        echo   [OK] Python %%v found via py
    )
    set "PYTHON_CMD=py"
    set "PYTHON_FOUND=1"
    goto :PythonFound
)

echo   [WARN] Python 3.11+ not found. Python bindings will be disabled.
echo          Install via: winget install Python.Python.3.12

:PythonFound

REM ---------------------------------------------------------------------------
REM Step 8: Clean build directories if requested
REM ---------------------------------------------------------------------------
echo.
echo [Step 8/12] Preparing build directories...

if %CLEAN_BUILD% EQU 1 (
    echo   Cleaning build directories...
    if exist "%BUILD_DIR%" (
        rmdir /s /q "%BUILD_DIR%" 2>nul
        echo     Removed: %BUILD_DIR%
    )
    if exist "%PACKAGE_DIR%" (
        rmdir /s /q "%PACKAGE_DIR%" 2>nul
        echo     Removed: %PACKAGE_DIR%
    )
    if exist "%DIST_DIR%" (
        rmdir /s /q "%DIST_DIR%" 2>nul
        echo     Removed: %DIST_DIR%
    )
    echo   Clean complete.
    exit /b 0
)

REM Create directories
if not exist "%BUILD_DIR%" (
    mkdir "%BUILD_DIR%" 2>nul
    echo   Created: %BUILD_DIR%
)
if not exist "%PACKAGE_DIR%" (
    mkdir "%PACKAGE_DIR%" 2>nul
    echo   Created: %PACKAGE_DIR%
)
if not exist "%DIST_DIR%" (
    mkdir "%DIST_DIR%" 2>nul
    echo   Created: %DIST_DIR%
)

REM ---------------------------------------------------------------------------
REM Step 9: Configure MSVC environment and CMake
REM ---------------------------------------------------------------------------
echo.
echo [Step 9/12] Configuring MSVC environment and CMake (%BUILD_TYPE%)...

REM Call vcvars64.bat to set up the build environment
echo   Setting up MSVC environment...
if exist "%VS_INSTALL_PATH%\VC\Auxiliary\Build\vcvars64.bat" (
    call "%VS_INSTALL_PATH%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
) else if exist "%VS_INSTALL_PATH%\VC\Auxiliary\Build\vcvarsall.bat" (
    call "%VS_INSTALL_PATH%\VC\Auxiliary\Build\vcvarsall.bat" x64 >nul 2>&1
) else (
    echo [ERROR] Could not configure MSVC environment.
    exit /b 1
)

echo   [OK] MSVC environment configured

REM Build CMake arguments
set "CMAKE_ARGS=-S "%PROJECT_ROOT%" -B "%BUILD_DIR%""
set "CMAKE_ARGS=%CMAKE_ARGS% -G "Ninja Multi-Config""
set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_INSTALL_PREFIX="%PACKAGE_DIR%""

REM Add vcpkg toolchain
if defined VCPKG_TOOLCHAIN (
    if exist "%VCPKG_TOOLCHAIN%" (
        set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_TOOLCHAIN_FILE="%VCPKG_TOOLCHAIN%""
        set "CMAKE_ARGS=%CMAKE_ARGS% -DVCPKG_TARGET_TRIPLET=x64-windows"
        echo   [OK] vcpkg toolchain: %VCPKG_TOOLCHAIN%
    )
)

REM Add Qt6 prefix
if defined QT6_DIR (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_PREFIX_PATH="%QT6_DIR%""
    echo   [OK] Qt6 prefix: %QT6_DIR%
)

REM Build options for all 23 modules
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_UI=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_SCADA=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_SIMULATION=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_IDE=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_AI=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_MODELS=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_HARMONICS=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_MARKETS=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_RELIABILITY=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_LICENSING=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_INTEGRATION=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_ICON_ENGINE=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_I18N=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_IO=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_GIS=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_XTALK=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_LEGAL=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_AUDIO=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_HELP=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_LINE_DESIGN=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_CONFIG=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_PACKAGING=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DENABLE_OPENMP=ON"
set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_SHARED_LIBS=OFF"

REM Python option
if %PYTHON_FOUND% EQU 1 (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_PYTHON=ON"
    echo   [OK] Python bindings enabled
) else (
    set "CMAKE_ARGS=%CMAKE_ARGS% -DBUILD_PYTHON=OFF"
    echo   Python bindings disabled
)

echo.
echo   Running CMake configuration...
echo   Command: cmake %CMAKE_ARGS%
echo.

cmake %CMAKE_ARGS%
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configuration failed with error code %ERRORLEVEL%.
    echo         Check the output above for details.
    exit /b 1
)
echo   [OK] CMake configuration completed

REM ---------------------------------------------------------------------------
REM Step 10: Build all 23 modules
REM ---------------------------------------------------------------------------
echo.
echo [Step 10/12] Building all 23 modules (%BUILD_TYPE%)...

echo   Starting parallel build...
echo.

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed with error code %ERRORLEVEL%.
    echo         Check the output above for compilation errors.
    exit /b 1
)
echo   [OK] Build completed successfully

REM ---------------------------------------------------------------------------
REM Step 11: Package with windeployqt6 and assemble
REM ---------------------------------------------------------------------------
echo.
echo [Step 11/12] Packaging application...

REM Create package subdirectories
for %%d in (
    "%PACKAGE_DIR%\bin"
    "%PACKAGE_DIR%\lib"
    "%PACKAGE_DIR%\python"
    "%PACKAGE_DIR%\resources"
    "%PACKAGE_DIR%\i18n"
    "%PACKAGE_DIR%\help"
    "%PACKAGE_DIR%\database\migrations"
    "%PACKAGE_DIR%\database\queries"
    "%PACKAGE_DIR%\database\seeds"
    "%PACKAGE_DIR%\ai\python\prompts"
    "%PACKAGE_DIR%\docs"
    "%PACKAGE_DIR%\legal"
    "%PACKAGE_DIR%\config"
    "%PACKAGE_DIR%\logs"
) do (
    if not exist "%%d" mkdir "%%d" 2>nul
)

REM --- Copy main executable ---
echo   Copying executable...
set "EXE_FOUND=0"

for %%p in (
    "%BUILD_DIR%\%BUILD_TYPE%\%PRODUCT_NAME%.exe"
    "%BUILD_DIR%\bin\%BUILD_TYPE%\%PRODUCT_NAME%.exe"
    "%BUILD_DIR%\%PRODUCT_NAME%.exe"
    "%BUILD_DIR%\bin\%PRODUCT_NAME%.exe"
) do (
    if exist "%%p" (
        copy /y "%%p" "%PACKAGE_DIR%\bin\" >nul 2>&1
        if %ERRORLEVEL% EQU 0 (
            echo     [OK] Copied %PRODUCT_NAME%.exe
            set "EXE_FOUND=1"
            goto :ExeCopied
        )
    )
)

if %EXE_FOUND% EQU 0 (
    echo   [WARN] %PRODUCT_NAME%.exe not found in expected locations.
    echo          Searched:
    echo            %BUILD_DIR%\%BUILD_TYPE%\%PRODUCT_NAME%.exe
    echo            %BUILD_DIR%\bin\%BUILD_TYPE%\%PRODUCT_NAME%.exe
    echo            %BUILD_DIR%\%PRODUCT_NAME%.exe
    echo            %BUILD_DIR%\bin\%PRODUCT_NAME%.exe
)
:ExeCopied

REM --- Copy DLLs from build directory ---
echo   Copying DLLs...
if exist "%BUILD_DIR%\%BUILD_TYPE%\*.dll" (
    for %%f in ("%BUILD_DIR%\%BUILD_TYPE%\*.dll") do (
        if /I not "%%~nxf"=="Qt6Core.dll" if /I not "%%~nxf"=="Qt6Gui.dll" if /I not "%%~nxf"=="Qt6Widgets.dll" if /I not "%%~nxf"=="Qt6Quick.dll" if /I not "%%~nxf"=="Qt6QuickControls2.dll" if /I not "%%~nxf"=="Qt6Network.dll" if /I not "%%~nxf"=="Qt6Sql.dll" if /I not "%%~nxf"=="Qt6Charts.dll" if /I not "%%~nxf"=="Qt6Qml.dll" if /I not "%%~nxf"=="Qt6QmlModels.dll" if /I not "%%~nxf"=="Qt6OpenGL.dll" if /I not "%%~nxf"=="Qt6Svg.dll" (
            copy /y "%%f" "%PACKAGE_DIR%\bin\" >nul 2>&1
        )
    )
)

REM --- Deploy Qt6 with windeployqt6 ---
echo   Deploying Qt6 runtime with windeployqt6...
if exist "%WINDEPLOYQT%" (
    if exist "%PACKAGE_DIR%\bin\%PRODUCT_NAME%.exe" (
        "%WINDEPLOYQT%" ^
            --%BUILD_TYPE% ^
            --dir "%PACKAGE_DIR%\bin" ^
            --plugindir "%PACKAGE_DIR%\bin\plugins" ^
            --qmlplugindir "%PACKAGE_DIR%\bin\qml" ^
            --qmldir "%PROJECT_ROOT%\ui\qml" ^
            --no-translations ^
            --no-system-d3d-compiler ^
            --no-virtualkeyboard ^
            --compiler-runtime ^
            --force ^
            "%PACKAGE_DIR%\bin\%PRODUCT_NAME%.exe"
        if %ERRORLEVEL% EQU 0 (
            echo     [OK] Qt6 deployment completed
        ) else (
            echo   [WARN] windeployqt6 returned code %ERRORLEVEL%
        )
    ) else (
        echo   [WARN] Executable not found for Qt6 deployment
    )
) else (
    echo   [WARN] windeployqt6.exe not found, skipping Qt6 deployment
)

REM --- Copy resources ---
echo   Copying resources...
if exist "%PROJECT_ROOT%\resources\icon.svg" (
    copy /y "%PROJECT_ROOT%\resources\icon.svg" "%PACKAGE_DIR%\resources\" >nul 2>&1
    echo     icon.svg
)

REM --- Copy i18n translations ---
echo   Copying translations...
if exist "%PROJECT_ROOT%\i18n\*.ts" (
    copy /y "%PROJECT_ROOT%\i18n\*.ts" "%PACKAGE_DIR%\i18n\" >nul 2>&1
    REM Compile .ts to .qm if lrelease is available
    if exist "%QT6_BIN_DIR%\lrelease.exe" (
        for %%f in ("%PROJECT_ROOT%\i18n\*.ts") do (
            "%QT6_BIN_DIR%\lrelease.exe" "%%f" -qm "%PACKAGE_DIR%\i18n\%%~nf.qm" >nul 2>&1
            if exist "%PACKAGE_DIR%\i18n\%%~nf.qm" (
                echo     %%~nf.qm
            )
        )
    )
    echo     All .ts files copied
)

REM --- Copy database files ---
echo   Copying database files...
if exist "%PROJECT_ROOT%\database\schema.sql" (
    copy /y "%PROJECT_ROOT%\database\schema.sql" "%PACKAGE_DIR%\database\" >nul 2>&1
    echo     schema.sql
)
if exist "%PROJECT_ROOT%\database\migrations\" (
    copy /y "%PROJECT_ROOT%\database\migrations\*" "%PACKAGE_DIR%\database\migrations\" >nul 2>&1
    echo     migrations/
)
if exist "%PROJECT_ROOT%\database\queries\" (
    copy /y "%PROJECT_ROOT%\database\queries\*" "%PACKAGE_DIR%\database\queries\" >nul 2>&1
    echo     queries/
)
if exist "%PROJECT_ROOT%\database\seeds\" (
    copy /y "%PROJECT_ROOT%\database\seeds\*" "%PACKAGE_DIR%\database\seeds\" >nul 2>&1
    echo     seeds/
)

REM --- Copy help files ---
echo   Copying help files...
if exist "%PROJECT_ROOT%\help\" (
    copy /y "%PROJECT_ROOT%\help\*" "%PACKAGE_DIR%\help\" >nul 2>&1
    echo     help/
)

REM --- Copy AI prompts and scripts ---
echo   Copying AI integration files...
if exist "%PROJECT_ROOT%\ai\python\" (
    xcopy /s /e /i /y "%PROJECT_ROOT%\ai\python" "%PACKAGE_DIR%\ai\python\" >nul 2>&1
    echo     ai/python/
)

REM --- Copy Python package ---
if %PYTHON_FOUND% EQU 1 (
    echo   Copying Python package...
    if exist "%PROJECT_ROOT%\python\powsy365\" (
        xcopy /s /e /i /y "%PROJECT_ROOT%\python\powsy365" "%PACKAGE_DIR%\python\powsy365\" >nul 2>&1
        echo     powsy365/
    )
    if exist "%PROJECT_ROOT%\python\setup.py" (
        copy /y "%PROJECT_ROOT%\python\setup.py" "%PACKAGE_DIR%\python\" >nul 2>&1
    )
    if exist "%PROJECT_ROOT%\python\requirements.txt" (
        copy /y "%PROJECT_ROOT%\python\requirements.txt" "%PACKAGE_DIR%\python\" >nul 2>&1
    )
)

REM --- Copy documentation ---
echo   Copying documentation...
if exist "%PROJECT_ROOT%\README.md" (
    copy /y "%PROJECT_ROOT%\README.md" "%PACKAGE_DIR%\docs\" >nul 2>&1
    echo     README.md
)
if exist "%PROJECT_ROOT%\LICENSE" (
    copy /y "%PROJECT_ROOT%\LICENSE" "%PACKAGE_DIR%\" >nul 2>&1
    echo     LICENSE
)
if exist "%PROJECT_ROOT%\docs\" (
    xcopy /s /e /i /y "%PROJECT_ROOT%\docs" "%PACKAGE_DIR%\docs\" >nul 2>&1
    echo     docs/
)

REM --- Copy legal files ---
echo   Copying legal files...
if exist "%PROJECT_ROOT%\legal\" (
    xcopy /s /e /i /y "%PROJECT_ROOT%\legal" "%PACKAGE_DIR%\legal\" >nul 2>&1
    echo     legal/
)

REM --- Copy QML files ---
echo   Copying QML files...
if exist "%PROJECT_ROOT%\ui\qml\" (
    if not exist "%PACKAGE_DIR%\bin\qml" mkdir "%PACKAGE_DIR%\bin\qml" 2>nul
    copy /y "%PROJECT_ROOT%\ui\qml\*.qml" "%PACKAGE_DIR%\bin\qml\" >nul 2>&1
    if exist "%PROJECT_ROOT%\ui\qml\qmldir" (
        copy /y "%PROJECT_ROOT%\ui\qml\qmldir" "%PACKAGE_DIR%\bin\qml\" >nul 2>&1
    )
    echo     QML files copied
)

REM --- Create .qmldir for deployed QML ---
if exist "%PACKAGE_DIR%\bin\qml" (
    echo module POWSYS365.QML > "%PACKAGE_DIR%\bin\qml\deployed.qmldir"
    echo # Auto-generated by build.bat >> "%PACKAGE_DIR%\bin\qml\deployed.qmldir"
)

REM --- Create install manifest ---
(
    echo %PRODUCT_NAME% v%PRODUCT_VERSION%
    echo Build type: %BUILD_TYPE%
    echo Architecture: %ARCH%
    echo Build date: %date% %time%
    echo Windows: %WIN_MAJOR%.%WIN_MINOR%
    echo CMake: %CMAKE_VERSION%
    echo Qt6: %QT6_DIR%
) > "%PACKAGE_DIR%\.install_manifest"

echo   [OK] Package assembly completed

REM ---------------------------------------------------------------------------
REM Step 12: Create ZIP archive
REM ---------------------------------------------------------------------------
echo.
echo [Step 12/12] Creating portable ZIP archive...

set "ZIP_NAME=%PRODUCT_NAME%-%PRODUCT_VERSION%-windows-%ARCH%-portable.zip"
set "ZIP_PATH=%DIST_DIR%\%ZIP_NAME%"

REM Remove existing ZIP
if exist "%ZIP_PATH%" (
    del /f "%ZIP_PATH%" >nul 2>&1
    echo   Removed existing: %ZIP_NAME%
)

REM Use PowerShell's Compress-Archive
powershell -NoProfile -Command "Compress-Archive -Path '%PACKAGE_DIR%\*' -DestinationPath '%ZIP_PATH%' -CompressionLevel Optimal -Force" >nul 2>&1

if %ERRORLEVEL% EQU 0 (
    if exist "%ZIP_PATH%" (
        for %%F in ("%ZIP_PATH%") do (
            set "ZIP_SIZE=%%~zF"
        )
        echo   [OK] Created: %ZIP_NAME%
        echo        Path: %ZIP_PATH%
        echo        Size: %ZIP_SIZE% bytes
    ) else (
        echo   [WARN] ZIP creation reported success but file not found
    )
) else (
    echo   [WARN] PowerShell Compress-Archive failed, trying 7-Zip...
    
    where 7z >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        7z a -tzip -mx=9 "%ZIP_PATH%" "%PACKAGE_DIR%\*" >nul 2>&1
        if exist "%ZIP_PATH%" (
            echo   [OK] Created with 7-Zip: %ZIP_NAME%
        ) else (
            echo   [WARN] 7-Zip also failed
        )
    ) else (
        echo   [WARN] Could not create ZIP. Install 7-Zip or ensure PowerShell is available.
    )
)

REM ---------------------------------------------------------------------------
REM Build Summary
REM ---------------------------------------------------------------------------
echo.
echo ================================================================
echo               %PRODUCT_NAME% Build Report
echo ================================================================
echo   Product:      %PRODUCT_NAME% v%PRODUCT_VERSION%
echo   Publisher:    %PUBLISHER%
echo   Architecture: %ARCH%
echo   Build Type:   %BUILD_TYPE%
echo.
echo   Directories:
echo     Build:      %BUILD_DIR%
echo     Package:    %PACKAGE_DIR%
echo     Output:     %DIST_DIR%
echo.
echo   Environment:
echo     Visual Studio: %VS_INSTALL_PATH%
echo     CMake:         %CMAKE_CMD%
echo     Qt6:           %QT6_DIR%
echo     vcpkg:         %VCPKG_ROOT%
echo     Python:        %PYTHON_CMD%
echo.
echo   Generated Artifacts:
if exist "%ZIP_PATH%" (
    for %%F in ("%ZIP_PATH%") do (
        set "FINAL_SIZE=%%~zF"
    )
    echo     - %ZIP_NAME% (!FINAL_SIZE! bytes)
)
echo.
echo ================================================================
echo   Build completed at: %date% %time%
echo ================================================================
echo.

endlocal
goto :EOF

REM ============================================================================
REM HELP SUBROUTINES
REM ============================================================================

:ShowHelp
echo.
echo POWSYS365 Windows Build Script
echo ==============================
echo.
echo Usage: build.bat [option]
echo.
echo Options:
echo   (none)              Default Release build
echo   debug               Debug build with symbols
echo   relwithdebinfo      Release with debug information
echo   clean               Clean all build directories
echo   --help, /help, /?   Show this help message
echo.
echo Examples:
echo   build.bat                          Build Release version
echo   build.bat debug                    Build Debug version
echo   build.bat clean                    Remove all build artifacts
echo   build.bat relwithdebinfo           Build RelWithDebInfo
echo.
echo Prerequisites:
echo   - Visual Studio 2022 or Build Tools 2022
echo   - CMake 3.31+ (in PATH)
echo   - vcpkg (set VCPKG_ROOT env var)
echo   - Qt6 6.5+ (set QT6_DIR env var)
echo   - Python 3.11+ (optional)
echo.
echo Environment Variables:
echo   VCPKG_ROOT          Path to vcpkg installation
echo   QT6_DIR             Path to Qt6 (e.g., C:\Qt\6.8.2\msvc2022_64)
echo   QTDIR               Fallback for Qt6 path
echo.
echo Output:
echo   - Build:    PROJECT_ROOT\build-windows\build
echo   - Package:  PROJECT_ROOT\build-windows\package
echo   - Archive:  PROJECT_ROOT\build-windows\dist\*.zip
echo.
exit /b 0
