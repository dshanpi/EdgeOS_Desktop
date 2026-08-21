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
RESOURCE="${SDK_RTSMART_SRC_DIR}/examples/ai/yolo/utils"
mkdir -p "${BUILD_DIR}" "${BIN_DIR}/models"
cmake -S "${SCRIPTPATH}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${SCRIPTPATH}/../hand_studio/cmake/Riscv64.cmake"
cmake --build "${BUILD_DIR}" --parallel "${BUILD_JOBS:-2}"
cmake --install "${BUILD_DIR}"
cp -f "${BUILD_DIR}/bin/yolo_models.elf" "${BIN_DIR}/"
cp -f "${RESOURCE}/coco_labels.txt" "${BIN_DIR}/"
cp -f "${RESOURCE}/yolov5n.kmodel" "${BIN_DIR}/models/"
cp -f "${RESOURCE}/yolov8n.kmodel" "${BIN_DIR}/models/"
cp -f "${RESOURCE}/yolo11n.kmodel" "${BIN_DIR}/models/"
cp -f "${RESOURCE}/yolo26n.kmodel" "${BIN_DIR}/models/"
echo "[yolo-models] package ready: ${BIN_DIR}"
