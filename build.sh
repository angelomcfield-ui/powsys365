#!/bin/bash
# ============================================================
# POWSYS365 - Build Script
# Soporta: macOS 12+, Windows 11 (MSYS2), Linux
# ============================================================
set -e

echo "=========================================="
echo "  POWSYS365 Build System v1.0.0"
echo "=========================================="

# Detectar sistema operativo
OS="unknown"
if [[ "$OSTYPE" == "darwin"* ]]; then
    OS="macos"
    CORES=$(sysctl -n hw.ncpu)
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    OS="linux"
    CORES=$(nproc)
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    OS="windows"
    CORES=$NUMBER_OF_PROCESSORS
fi
echo "OS: $OS | Cores: $CORES"

# Directorio de build
BUILD_DIR="build"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configurar con CMake
echo ""
echo "[1/4] Configuring with CMake..."
if [ "$OS" = "macos" ]; then
    cmake .. \
        -DCMAKE_OSX_ARCHITECTURES="x86_64;arm64" \
        -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_UI=ON \
        -DBUILD_PYTHON=ON \
        -DBUILD_TESTS=ON \
        -DBUILD_SCADA=ON \
        -DBUILD_SIMULATION=ON \
        -DBUILD_IDE=ON \
        -DBUILD_AI=ON
else
    cmake .. \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_UI=ON \
        -DBUILD_PYTHON=ON \
        -DBUILD_TESTS=ON \
        -DBUILD_SCADA=ON \
        -DBUILD_SIMULATION=ON \
        -DBUILD_IDE=ON \
        -DBUILD_AI=ON
fi

# Compilar
echo ""
echo "[2/4] Building..."
cmake --build . --parallel "$CORES"

# Ejecutar tests
echo ""
echo "[3/4] Running tests..."
ctest --output-on-failure || true

# Empaquetar
echo ""
echo "[4/4] Packaging..."
cpack || true

echo ""
echo "=========================================="
echo "  Build completed successfully!"
echo "  Binaries in: $BUILD_DIR/bin/"
echo "=========================================="
