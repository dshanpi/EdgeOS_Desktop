set(common ${PROJECT_SOURCE_DIR}/../face_studio/src)
set(demo ${PROJECT_SOURCE_DIR}/../ai_demo)
set(src
    main.cc human_studio.cc person_runner.cc pose_runner.cc fall_runner.cc
    ${demo}/person_detect/person_detect.cc
    ${demo}/pose_detect/pose_detect.cc
    ${demo}/falldown_detect/falldown_detect.cc
    ${common}/ai_base.cc ${common}/ai_utils.cc
    ${common}/ui_localization.cc
    ${common}/video_pipeline.cc ${common}/drv_touch.c
    ${VAXP_AI_SOURCES})
set(bin human_studio.elf)
include_directories(${PROJECT_SOURCE_DIR}/src ${common}
    ${demo}/person_detect ${demo}/pose_detect ${demo}/falldown_detect
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
