#!/bin/bash
# ============================================================
# POWSYS365 - Dependency Installation Script
# ============================================================
set -e

echo "Installing POWSYS365 dependencies..."

if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "macOS detected - using Homebrew"
    brew install cmake eigen pybind11 qt@6 postgresql openssl catch2 nlohmann-json
    brew link qt@6 --force
elif [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Linux detected - using apt"
    sudo apt-get update
    sudo apt-get install -y cmake g++ libeigen3-dev pybind11-dev \
        libqt6-all-dev libpq-dev postgresql postgresql-contrib \
        libssl-dev catch2 nlohmann-json3-dev libqt6charts6-dev \
        libqt6websockets6-dev qt6-webengine-dev python3-dev python3-pip
    pip3 install numpy scipy pandas matplotlib plotly pandapower pybind11
elif [[ "$OSTYPE" == "msys" || "$OSTYPE" == "cygwin" ]]; then
    echo "Windows detected - using vcpkg"
    echo "Please install dependencies via vcpkg:"
    echo "  vcpkg install eigen3 pybind11 qt6 postgresql openssl catch2 nlohmann-json"
fi

echo "Dependencies installed!"
