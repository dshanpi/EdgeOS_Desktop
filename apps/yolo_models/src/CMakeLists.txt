set(common ${PROJECT_SOURCE_DIR}/../face_studio/src)
set(yolo ${PROJECT_SOURCE_DIR}/../../../../rtsmart/examples/ai/yolo/src)
set(src main.cc ${yolo}/ai_base.cc ${yolo}/utils.cc
    ${yolo}/yolov5.cc ${yolo}/yolov8.cc
    ${yolo}/yolo11.cc ${yolo}/yolo26.cc
    ${common}/video_pipeline.cc ${common}/drv_touch.c
    ${common}/ui_localization.cc ${VAXP_AI_SOURCES})
set(bin yolo_models.elf)
include_directories(${PROJECT_SOURCE_DIR}/src ${common} ${yolo}
    ${PROJECT_SOURCE_DIR}/../ai_demo/libs/3rd-party/include
    ${nncase_sdk_root}/riscv64/rvvlib/include
    ${k230_mpp_root}/userapps/api/ ${k230_mpp_root}/include
    ${k230_mpp_root}/include/comm
    ${k230_mpp_root}/userapps/sample/sample_vo)
link_directories(${nncase_sdk_root}/riscv64/rvvlib/)
link_directories(${PROJECT_SOURCE_DIR}/../ai_demo/libs/3rd-party/lib)
add_executable(${bin} ${src})
target_link_libraries(${bin}
    -Wl,--start-group rvv Nncase.Runtime.Native nncase.rt_modules.k230
    functional_k230 sys vicap vb isp vo connector sensor atomic gsdma
    opencv_imgcodecs opencv_imgproc opencv_core freetype zlib libjpeg-turbo
    libopenjp2 libpng libtiff libwebp csi_cv -Wl,--end-group)
install(TARGETS ${bin} DESTINATION bin)
