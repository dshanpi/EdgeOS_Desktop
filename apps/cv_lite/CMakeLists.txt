cmake_minimum_required(VERSION 3.2)
project(cv_lite C CXX)

set(CMAKE_EXE_LINKER_FLAGS "-T ${PROJECT_SOURCE_DIR}/../face_studio/cmake/link.lds --static")
set(k230_opencv $ENV{OPENCV_SRC_DIR})
set(k230_mpp_root $ENV{MPP_SRC_DIR})

include_directories(${k230_opencv}/include/opencv4/)
include_directories(${k230_mpp_root}/userapps/api/ ${k230_mpp_root}/include
                    ${k230_mpp_root}/include/comm
                    ${k230_mpp_root}/userapps/sample/sample_vo
                    ${PROJECT_SOURCE_DIR}/../face_studio/src
                    ${PROJECT_SOURCE_DIR}/../ai_demo/libs/3rd-party/include)
link_directories(${k230_opencv}/lib ${k230_opencv}/lib/opencv4/3rdparty
                 ${k230_mpp_root}/userapps/lib
                 ${PROJECT_SOURCE_DIR}/../ai_demo/libs/3rd-party/lib)

include(${PROJECT_SOURCE_DIR}/../cmake/vaxp_ai.cmake)

add_executable(cv_lite.elf src/main.cc
    ../face_studio/src/video_pipeline.cc ../face_studio/src/drv_touch.c
    ../face_studio/src/ui_localization.cc ${VAXP_AI_SOURCES})
target_link_libraries(cv_lite.elf
    -Wl,--start-group sys vicap vb isp vo connector sensor
    atomic gsdma -Wl,--end-group
    opencv_calib3d opencv_features2d opencv_flann
    opencv_imgcodecs opencv_imgproc opencv_core freetype zlib libjpeg-turbo
    libopenjp2 libpng libtiff libwebp csi_cv)
install(TARGETS cv_lite.elf DESTINATION bin)
