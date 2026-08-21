#!/bin/bash
# Build and package K230 example programs (incremental build)
# Function: Use CMake and cross-compile toolchain to build face_detection example and collect ELF and utility files

set -e  # Exit immediately if a command exits with a non-zero status
# set -x  # Enable debug mode

# =======================
# Script directory and SDK paths
# =======================
SCRIPT=$(realpath -s "$0")
SCRIPTPATH=$(dirname "$SCRIPT")

export SDK_SRC_ROOT_DIR=$(realpath "${SCRIPTPATH}/../../../../../")
export SDK_RTSMART_SRC_DIR="${SDK_SRC_ROOT_DIR}/src/rtsmart/"
export MPP_SRC_DIR="${SDK_RTSMART_SRC_DIR}/mpp/"
export FREETYPE_SRC_DIR="${SDK_RTSMART_SRC_DIR}/libs/3rd-party/freetype/"
export NNCASE_SRC_DIR="${SDK_RTSMART_SRC_DIR}/libs/nncase/"
export OPENCV_SRC_DIR="${SDK_RTSMART_SRC_DIR}/libs/opencv/"

# Set cross-compile toolchain path
export PATH=$PATH:~/.kendryte/k230_toolchains/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin

# =======================
# Output directories
# =======================
BUILD_DIR="${SCRIPTPATH}/build"
K230_BIN_DIR="${SCRIPTPATH}/k230_bin"
mkdir -p "${BUILD_DIR}"
mkdir -p "${K230_BIN_DIR}"

# =======================
# Target directory (optional argument)
# =======================
TARGET_DIR="$1"

# =======================
# Build function
# =======================
build_project() {
    pushd "${BUILD_DIR}"

    # Incremental build: only run CMake if CMakeCache.txt does not exist
    if [ ! -f "CMakeCache.txt" ]; then
        echo "Running initial CMake configuration..."
        if [ -n "${TARGET_DIR}" ]; then
            echo "Building target directory: ${TARGET_DIR}"
            cmake -DCMAKE_BUILD_TYPE=Release \
                  -DCMAKE_INSTALL_PREFIX="$(pwd)" \
                  -DCMAKE_TOOLCHAIN_FILE=../cmake/Riscv64.cmake \
                  -DTARGET_DIR="${TARGET_DIR}" \
                  ..
        else
            echo "Building all modules"
            cmake -DCMAKE_BUILD_TYPE=Release \
                  -DCMAKE_INSTALL_PREFIX="$(pwd)" \
                  -DCMAKE_TOOLCHAIN_FILE=../cmake/Riscv64.cmake \
                  ..
        fi
    else
        echo "Using existing CMake configuration for incremental build..."
    fi

    echo "Starting build..."
    # Keep the default conservative for SDK builds running in small VMs.
    # Callers may opt in to more parallelism with BUILD_JOBS=<n>.
    make -j"${BUILD_JOBS:-1}" --no-print-directory

    echo "Installing build artifacts..."
    make install --no-print-directory

    popd
}

# =======================
# Collect ELF and utility files
# =======================
collect_outputs() {
    echo "Collecting ELF and utility files to ${K230_BIN_DIR}..."
    cp -u "${BUILD_DIR}/bin/"*.elf "${K230_BIN_DIR}" 2>/dev/null || true
    cp -u utils/* "${K230_BIN_DIR}" 2>/dev/null || true
}

# =======================
# Main process
# =======================
build_project
collect_outputs

echo "Build finished. Output directory: ${K230_BIN_DIR}"
