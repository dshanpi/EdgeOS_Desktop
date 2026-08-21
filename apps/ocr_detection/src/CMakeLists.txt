set(common ${PROJECT_SOURCE_DIR}/../face_studio/src)
set(ocr ${PROJECT_SOURCE_DIR}/../ai_demo/ocr)
set(src main.cc ${ocr}/ocr_box.cc ${ocr}/ocr_reco.cc
    ${ocr}/clipper.cpp ${ocr}/paint_text.cc
    ${common}/ai_base.cc ${common}/ai_utils.cc
    ${common}/video_pipeline.cc ${common}/ui_localization.cc
    ${common}/drv_touch.c ${VAXP_AI_SOURCES})
set(bin ocr_detection.elf)
include_directories(${PROJECT_SOURCE_DIR}/src ${common} ${ocr}
    ${nncase_sdk_root}/riscv64/rvvlib/include
    ${k230_mpp_root}/userapps/api/ ${k230_mpp_root}/include
    ${k230_mpp_root}/include/comm
    ${k230_mpp_root}/userapps/sample/sample_vo)
link_directories(${nncase_sdk_root}/riscv64/rvvlib/)
add_executable(${bin} ${src})
target_link_libraries(${bin}
    -Wl,--start-group rvv Nncase.Runtime.Native nncase.rt_modules.k230
    functional_k230 sys vicap vb isp vo connector sensor atomic gsdma
    opencv_imgcodecs opencv_imgproc opencv_core zlib libjpeg-turbo
    libopenjp2 libpng libtiff libwebp csi_cv freetype -Wl,--end-group)
install(TARGETS ${bin} DESTINATION bin)
