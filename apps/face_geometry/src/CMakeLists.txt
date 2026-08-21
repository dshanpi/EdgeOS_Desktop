set(FACE_STUDIO_SRC ${PROJECT_SOURCE_DIR}/../face_studio/src)
set(AI_DEMO_SRC ${PROJECT_SOURCE_DIR}/../ai_demo)
set(src
    main.cc geometry_studio.cc
    ${FACE_STUDIO_SRC}/face_detection.cc
    ${FACE_STUDIO_SRC}/anchors_320.cc ${FACE_STUDIO_SRC}/anchors_640.cc
    ${FACE_STUDIO_SRC}/ai_base.cc ${FACE_STUDIO_SRC}/ai_utils.cc
    ${FACE_STUDIO_SRC}/ui_localization.cc
    ${FACE_STUDIO_SRC}/video_pipeline.cc ${FACE_STUDIO_SRC}/drv_touch.c
    ${AI_DEMO_SRC}/face_pose/face_pose.cc
    ${AI_DEMO_SRC}/face_parse/face_parse.cc
    ${AI_DEMO_SRC}/face_mesh/face_mesh.cc
    ${AI_DEMO_SRC}/face_mesh/face_mesh_post.cc
    ${AI_DEMO_SRC}/face_alignment/face_alignment.cc
    ${AI_DEMO_SRC}/face_alignment/face_alignment_post.cc
    ${AI_DEMO_SRC}/face_alignment/rasterize.cc
    ${VAXP_AI_SOURCES})
set(bin face_geometry.elf)
include_directories(
    ${PROJECT_SOURCE_DIR}/src ${FACE_STUDIO_SRC}
    ${AI_DEMO_SRC}/libs/3rd-party/include
    ${AI_DEMO_SRC}/face_pose ${AI_DEMO_SRC}/face_parse
    ${AI_DEMO_SRC}/face_mesh ${AI_DEMO_SRC}/face_alignment
    ${nncase_sdk_root}/riscv64/rvvlib/include
    ${k230_mpp_root}/userapps/api/ ${k230_mpp_root}/include
    ${k230_mpp_root}/include/comm
    ${k230_mpp_root}/userapps/sample/sample_vo)
link_directories(${nncase_sdk_root}/riscv64/rvvlib/)
link_directories(${AI_DEMO_SRC}/libs/3rd-party/lib)
add_executable(${bin} ${src})
target_link_libraries(${bin}
    -Wl,--start-group rvv Nncase.Runtime.Native nncase.rt_modules.k230
    functional_k230 sys vicap vb isp vo connector sensor atomic gsdma
    -Wl,--end-group)
target_link_libraries(${bin}
    opencv_imgcodecs opencv_imgproc opencv_core freetype zlib libjpeg-turbo
    libopenjp2 libpng libtiff libwebp csi_cv)
install(TARGETS ${bin} DESTINATION bin)
