cmake_minimum_required(VERSION 3.2)
project(code_scanner C CXX)
set(CMAKE_EXE_LINKER_FLAGS "-T ${PROJECT_SOURCE_DIR}/../face_studio/cmake/link.lds --static")
set(opencv $ENV{OPENCV_SRC_DIR})
set(mpp $ENV{MPP_SRC_DIR})
include_directories(${opencv}/include/opencv4 ${mpp}/userapps/api ${mpp}/include
    ${mpp}/include/comm ${mpp}/userapps/sample/sample_vo ../face_studio/src
    ../ai_demo/libs/3rd-party/include)
link_directories(${opencv}/lib ${opencv}/lib/opencv4/3rdparty ${mpp}/userapps/lib
    ../ai_demo/libs/3rd-party/lib)
add_executable(code_scanner.elf src/main.cc ../face_studio/src/video_pipeline.cc
    ../face_studio/src/drv_touch.c ../face_studio/src/ui_localization.cc)
target_link_libraries(code_scanner.elf -Wl,--start-group sys vicap vb isp vo connector
    sensor atomic gsdma -Wl,--end-group opencv_calib3d opencv_features2d
    opencv_flann opencv_objdetect quirc opencv_imgcodecs opencv_imgproc opencv_core
    freetype zlib libjpeg-turbo libopenjp2 libpng libtiff libwebp csi_cv)
install(TARGETS code_scanner.elf DESTINATION bin)
