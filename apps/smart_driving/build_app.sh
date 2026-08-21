#!/bin/bash
set -e
SCRIPT=$(realpath -s "$0")
SCRIPTPATH=$(dirname "$SCRIPT")
export SDK_SRC_ROOT_DIR=$(realpath "${SCRIPTPATH}/../../../../../")
export SDK_RTSMART_SRC_DIR="${SDK_SRC_ROOT_DIR}/src/rtsmart/"
export MPP_SRC_DIR="${SDK_RTSMART_SRC_DIR}/mpp/"
export NNCASE_SRC_DIR="${SDK_RTSMART_SRC_DIR}/libs/nncase/"
export OPENCV_SRC_DIR="${SDK_RTSMART_SRC_DIR}/libs/opencv/"
export PATH="${PATH}:${K230_TOOLCHAIN_BIN:-${HOME}/.kendryte/k230_toolchains/riscv64-linux-musleabi_for_x86_64-pc-linux-gnu/bin}"
BUILD_DIR="${SCRIPTPATH}/build"
BIN_DIR="${SCRIPTPATH}/k230_bin"
MODEL_DIR="${SCRIPTPATH}/../ai_demo/resources/ai_poc/kmodel"
mkdir -p "${BUILD_DIR}" "${BIN_DIR}/models"
cmake -S "${SCRIPTPATH}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${SCRIPTPATH}/../hand_studio/cmake/Riscv64.cmake"
cmake --build "${BUILD_DIR}" --parallel "${BUILD_JOBS:-2}"
cmake --install "${BUILD_DIR}"
cp -f "${BUILD_DIR}/bin/smart_driving.elf" "${BIN_DIR}/"
cp -f "${MODEL_DIR}/crosswalk.kmodel" "${BIN_DIR}/models/"
cp -f "${MODEL_DIR}/traffic.kmodel" "${BIN_DIR}/models/"
cp -f "${MODEL_DIR}/helmet.kmodel" "${BIN_DIR}/models/"
cp -f "${MODEL_DIR}/yolov5s_smoke_best.kmodel" "${BIN_DIR}/models/"
echo "[smart-driving] package ready: ${BIN_DIR}"
