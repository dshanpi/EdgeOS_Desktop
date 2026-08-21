set(face_common ${PROJECT_SOURCE_DIR}/../face_studio/src)
set(hand_common ${PROJECT_SOURCE_DIR}/../ai_demo/sq_handkp_class)
set(hand_reco ${PROJECT_SOURCE_DIR}/../ai_demo/sq_handreco)

set(src
    main.cc hand_studio.cc recognition_runner.cc
    ${face_common}/ui_localization.cc
    ${hand_common}/hand_detection.cc
    ${hand_common}/hand_keypoint.cc
    ${hand_reco}/hand_recognition.cc
    ${face_common}/ai_base.cc
    ${face_common}/ai_utils.cc
    ${face_common}/video_pipeline.cc
    ${face_common}/drv_touch.c
    ${VAXP_AI_SOURCES})
set(bin hand_studio.elf)

include_directories(${PROJECT_SOURCE_DIR}/src)
set(freetype_root ${PROJECT_SOURCE_DIR}/../ai_demo/libs/3rd-party)
include_directories(${freetype_root}/include)
link_directories(${freetype_root}/lib)
include_directories(${hand_common})
include_directories(${face_common})
include_directories(${nncase_sdk_root}/riscv64/rvvlib/include)
link_directories(${nncase_sdk_root}/riscv64/rvvlib/)
include_directories(${k230_mpp_root}/userapps/api/)
include_directories(${k230_mpp_root}/include)
include_directories(${k230_mpp_root}/include/comm)
include_directories(${k230_mpp_root}/userapps/sample/sample_vo)

add_executable(${bin} ${src})
target_link_libraries(${bin}
    -Wl,--start-group rvv Nncase.Runtime.Native nncase.rt_modules.k230
    functional_k230 sys vicap vb isp vo connector sensor atomic gsdma
    -Wl,--end-group)
target_link_libraries(${bin}
    opencv_imgcodecs opencv_imgproc opencv_core freetype zlib libjpeg-turbo
    libopenjp2 libpng libtiff libwebp csi_cv)
install(TARGETS ${bin} DESTINATION bin)
