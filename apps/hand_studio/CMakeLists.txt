cmake_minimum_required(VERSION 3.2)
project(hand_studio C CXX)

set(CMAKE_EXE_LINKER_FLAGS
    "-T ${PROJECT_SOURCE_DIR}/../face_studio/cmake/link.lds --static")

set(k230_opencv $ENV{OPENCV_SRC_DIR})
include_directories(${k230_opencv}/include/opencv4/)
link_directories(${k230_opencv}/lib ${k230_opencv}/lib/opencv4/3rdparty)

set(k230_mpp_root "$ENV{MPP_SRC_DIR}")
link_directories(${k230_mpp_root}/userapps/lib)

set(nncase_sdk_root $ENV{NNCASE_SRC_DIR})
include_directories(${nncase_sdk_root}/riscv64)
include_directories(${nncase_sdk_root}/riscv64/nncase/include)
include_directories(${nncase_sdk_root}/riscv64/nncase/include/nncase/runtime)
include_directories(${nncase_sdk_root}/riscv64/rvvlib/include)
link_directories(${nncase_sdk_root}/riscv64/nncase/lib/)

include(${PROJECT_SOURCE_DIR}/../cmake/vaxp_ai.cmake)

add_subdirectory(src)
