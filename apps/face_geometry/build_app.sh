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
RESOURCE_ROOT="${SCRIPTPATH}/../ai_demo/resources/ai_poc"
mkdir -p "${BUILD_DIR}" "${BIN_DIR}/models"
cmake -S "${SCRIPTPATH}" -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${BUILD_DIR}" \
    -DCMAKE_TOOLCHAIN_FILE="${SCRIPTPATH}/../face_studio/cmake/Riscv64.cmake"
cmake --build "${BUILD_DIR}" --parallel "${BUILD_JOBS:-2}"
cmake --install "${BUILD_DIR}"
cp -f "${BUILD_DIR}/bin/face_geometry.elf" "${BIN_DIR}/"
for model in face_detection_320.kmodel face_pose.kmodel face_parse.kmodel \
             face_alignment.kmodel face_alignment_post.kmodel; do
    cp -f "${RESOURCE_ROOT}/kmodel/${model}" "${BIN_DIR}/models/"
done
cp -f "${RESOURCE_ROOT}/utils/bfm_tri.bin" "${BIN_DIR}/"
cp -f "${RESOURCE_ROOT}/utils/ncc_code.bin" "${BIN_DIR}/"
echo "[face-geometry] package ready: ${BIN_DIR}"
