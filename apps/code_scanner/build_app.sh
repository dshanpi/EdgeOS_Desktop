#!/bin/bash
set -e
ROOT=$(dirname "$(realpath -s "$0")")
export SDK_SRC_ROOT_DIR=$(realpath "${ROOT}/../../../../../")
export SDK_RTSMART_SRC_DIR="${SDK_SRC_ROOT_DIR}/src/rtsmart"
export MPP_SRC_DIR="${SDK_RTSMART_SRC_DIR}/mpp"
export OPENCV_SRC_DIR="${SDK_RTSMART_SRC_DIR}/libs/opencv"
export PATH="${PATH}:${K230_TOOLCHAIN_BIN:-${HOME}/.kendryte/k230_toolchains/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin}"
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$ROOT/build" \
  -DCMAKE_TOOLCHAIN_FILE="$ROOT/../face_studio/cmake/Riscv64.cmake"
cmake --build "$ROOT/build" --parallel "${BUILD_JOBS:-2}"
cmake --install "$ROOT/build"
mkdir -p "$ROOT/k230_bin"
cp -f "$ROOT/build/bin/code_scanner.elf" "$ROOT/k230_bin/"
