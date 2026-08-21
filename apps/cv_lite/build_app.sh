#!/bin/bash
set -e

SCRIPT=$(realpath -s "$0")
SCRIPTPATH=$(dirname "$SCRIPT")
export SDK_SRC_ROOT_DIR=$(realpath "${SCRIPTPATH}/../../../../../")
export SDK_RTSMART_SRC_DIR="${SDK_SRC_ROOT_DIR}/src/rtsmart/"
export MPP_SRC_DIR="${SDK_RTSMART_SRC_DIR}/mpp/"
export OPENCV_SRC_DIR="${SDK_RTSMART_SRC_DIR}/libs/opencv/"
export PATH="${PATH}:${K230_TOOLCHAIN_BIN:-${HOME}/.kendryte/k230_toolchains/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin}"

cmake -S "${SCRIPTPATH}" -B "${SCRIPTPATH}/build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${SCRIPTPATH}/build" \
    -DCMAKE_TOOLCHAIN_FILE="${SCRIPTPATH}/../face_studio/cmake/Riscv64.cmake"
cmake --build "${SCRIPTPATH}/build" --parallel "${BUILD_JOBS:-2}"
cmake --install "${SCRIPTPATH}/build"
mkdir -p "${SCRIPTPATH}/k230_bin"
cp -f "${SCRIPTPATH}/build/bin/cv_lite.elf" "${SCRIPTPATH}/k230_bin/"
echo "[cv-lite] package ready: ${SCRIPTPATH}/k230_bin"
