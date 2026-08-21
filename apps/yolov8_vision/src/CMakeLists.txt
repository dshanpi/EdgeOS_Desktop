set(common ${PROJECT_SOURCE_DIR}/../face_studio/src)
set(demo ${PROJECT_SOURCE_DIR}/../ai_demo)
set(src main.cc detection_runner.cc segmentation_runner.cc
    ${demo}/object_detect_yolov8n/ob_det.cc
    ${demo}/segment_yolov8n/segment.cc
    ${common}/ai_base.cc ${common}/ai_utils.cc
    ${common}/ui_localization.cc
    ${common}/video_pipeline.cc ${common}/drv_touch.c
    ${VAXP_AI_SOURCES})
set(bin yolov8_vision.elf)
include_directories(${PROJECT_SOURCE_DIR}/src ${common}
    ${demo}/object_detect_yolov8n ${demo}/segment_yolov8n
    ${demo}/libs/3rd-party/include
    ${nncase_sdk_root}/riscv64/rvvlib/include
    ${k230_mpp_root}/userapps/api/ ${k230_mpp_root}/include
    ${k230_mpp_root}/include/comm
    ${k230_mpp_root}/userapps/sample/sample_vo)
link_directories(${nncase_sdk_root}/riscv64/rvvlib/)
link_directories(${demo}/libs/3rd-party/lib)
add_executable(${bin} ${src})
target_link_libraries(${bin}
    -Wl,--start-group rvv Nncase.Runtime.Native nncase.rt_modules.k230
    functional_k230 sys vicap vb isp vo connector sensor atomic gsdma
    opencv_imgcodecs opencv_imgproc opencv_core freetype zlib libjpeg-turbo
    libopenjp2 libpng libtiff libwebp csi_cv -Wl,--end-group)
install(TARGETS ${bin} DESTINATION bin)
