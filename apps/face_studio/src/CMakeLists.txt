set(src
    main.cc face_studio.cc ui_localization.cc
    face_detection.cc face_emotion.cc face_gender.cc face_glasses.cc
    face_mask.cc eye_gaze.cc
    anchors_320.cc anchors_640.cc ai_base.cc ai_utils.cc
    video_pipeline.cc drv_touch.c ${VAXP_AI_SOURCES})
set(bin face_studio.elf)

include_directories(${PROJECT_SOURCE_DIR}/src)
set(freetype_root ${PROJECT_SOURCE_DIR}/../ai_demo/libs/3rd-party)
include_directories(${freetype_root}/include)
link_directories(${freetype_root}/lib)
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
