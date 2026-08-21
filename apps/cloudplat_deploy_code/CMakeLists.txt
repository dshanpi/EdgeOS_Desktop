cmake_minimum_required(VERSION 3.2)
project(ai_poc C CXX)

# set link script
set(CMAKE_EXE_LINKER_FLAGS "-T ${PROJECT_SOURCE_DIR}/cmake/link.lds --static")

# set nncase
set(nncase_sdk_root $ENV{NNCASE_SRC_DIR})
include_directories(${nncase_sdk_root}/riscv64)
include_directories(${nncase_sdk_root}/riscv64/nncase/include)
include_directories(${nncase_sdk_root}/riscv64/nncase/include/nncase/runtime)
include_directories(${nncase_sdk_root}/riscv64/rvvlib/include)
link_directories(${nncase_sdk_root}/riscv64/nncase/lib/)

# set opencv
set(k230_opencv $ENV{OPENCV_SRC_DIR})
include_directories(${k230_opencv}/include/opencv4/)
link_directories(${k230_opencv}/lib ${k230_opencv}/lib/opencv4/3rdparty)

# set freetype
include_directories(./libs/3rd-party/include)
link_directories(./libs/3rd-party/lib)

# set mpp and userapp
set(k230_mpp_root "$ENV{MPP_SRC_DIR}")
include_directories(${k230_mpp_root}/userapps/api/)
include_directories(${k230_mpp_root}/include)
include_directories(${k230_mpp_root}/include/comm)
include_directories(${k230_mpp_root}/userapps/sample/sample_vo)
link_directories(${nncase_sdk_root}/riscv64/rvvlib/)
link_directories(${k230_mpp_root}/userapps/lib)

# UART2 VAXP 1.0 result transport for all Cloud Model task binaries.
include(${PROJECT_SOURCE_DIR}/../cmake/vaxp_ai.cmake)

# ========== 条件添加子目录 ==========
# 如果没有传入 TARGET_DIR，就添加所有模块
if(NOT DEFINED TARGET_DIR OR TARGET_DIR STREQUAL "")
    message(STATUS "未指定 TARGET_DIR，构建全部模块")
    add_subdirectory(classification)
    add_subdirectory(detection)
    add_subdirectory(segmentation)
    add_subdirectory(ocr_detection)
    add_subdirectory(ocr_recognition)
    add_subdirectory(ocr)
    add_subdirectory(metric_learning)
    add_subdirectory(multilabel_cls)
else()
    message(STATUS "仅构建指定模块: ${TARGET_DIR}")
    add_subdirectory(${TARGET_DIR})
endif()
