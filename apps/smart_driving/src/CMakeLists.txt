set(common ${PROJECT_SOURCE_DIR}/../face_studio/src)
set(demo ${PROJECT_SOURCE_DIR}/../ai_demo)
set(src main.cc smart_driving.cc crosswalk_runner.cc traffic_runner.cc
    helmet_runner.cc smoke_runner.cc
    ${demo}/crosswalk_detect/crosswalk_detect.cc
    ${demo}/traffic_light_detect/traffic_light_detect.cc
    ${demo}/helmet_detect/helmet_detect.cc
    ${demo}/smoke_detect/smoke_detect.cc
    ${common}/ai_base.cc ${common}/ai_utils.cc
    ${common}/ui_localization.cc
    ${common}/video_pipeline.cc ${common}/drv_touch.c
    ${VAXP_AI_SOURCES})
set(bin smart_driving.elf)
include_directories(${PROJECT_SOURCE_DIR}/src ${common}
    ${demo}/crosswalk_detect ${demo}/traffic_light_detect
    ${demo}/helmet_detect ${demo}/smoke_detect
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
    -Wl,--end-group)
target_link_libraries(${bin}
    opencv_imgcodecs opencv_imgproc opencv_core freetype zlib libjpeg-turbo
    libopenjp2 libpng libtiff libwebp csi_cv)
install(TARGETS ${bin} DESTINATION bin)
