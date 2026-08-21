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

link_directories(./libs/sentencepiece/)

# set mpp and userapp
set(k230_mpp_root "$ENV{MPP_SRC_DIR}")
include_directories(${k230_mpp_root}/userapps/api/)
include_directories(${k230_mpp_root}/include)
include_directories(${k230_mpp_root}/include/comm)
link_directories(${nncase_sdk_root}/riscv64/rvvlib/)
link_directories(${k230_mpp_root}/userapps/lib)

# UART2 VAXP 1.0 AI result transport shared by the desktop AI applications.
include(${PROJECT_SOURCE_DIR}/../cmake/vaxp_ai.cmake)

# 定义要排除的目录列表
set(exclude_dirs "cmake" "k230_bin" "shell" "build" "common_files" ".git")
file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/*")

foreach(file ${files})
    # 判断当前文件是否是一个目录
    if(IS_DIRECTORY ${file})
        # 获取目录名称
        get_filename_component(dir_name ${file} NAME)
        # 检查目录是否在排除列表中
        list(FIND exclude_dirs ${dir_name} is_exclude_dir)
        # 如果目录不在排除列表中，则处理该目录
        if(${is_exclude_dir} EQUAL -1)
            # 处理该目录，例如添加到项目中
            if(${dir_name})
                add_subdirectory(${dir_name}) 
            endif()
        endif()
    endif()
endforeach()
