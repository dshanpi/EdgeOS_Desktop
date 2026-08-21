# set common files
set(common_files "../common_files")
include_directories(${common_files})
set(common_files_src
    ${common_files}/ai_base.cc
    ${common_files}/ai_utils.cc
    ${common_files}/video_pipeline.cc
)

# set source_code
set(touch_dir "${PROJECT_SOURCE_DIR}/../face_studio/src")
include_directories(${touch_dir})
set(nanotracker_dir "../nanotracker")
include_directories(${nanotracker_dir})
set(src main.cc self_learning.cc
    ${nanotracker_dir}/nanotrack_crop.cc
    ${nanotracker_dir}/nanotrack_src.cc
    ${nanotracker_dir}/nanotrack_tracker.cc
    ${touch_dir}/ui_localization.cc
    ${touch_dir}/drv_touch.c)
set(all_src ${src} ${common_files_src} ${VAXP_AI_SOURCES})
set(bin self_learning.elf)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(.)

add_executable(${bin} ${all_src})
target_link_libraries(${bin} -Wl,--start-group rvv Nncase.Runtime.Native nncase.rt_modules.k230 functional_k230 sys vicap vb isp vo connector sensor atomic gsdma -Wl,--end-group)

target_link_libraries(${bin}  -Wl,--start-group opencv_imgcodecs opencv_imgproc opencv_core zlib libjpeg-turbo libopenjp2 libpng libtiff libwebp csi_cv -Wl,--end-group)

target_link_libraries(${bin}  -Wl,--start-group freetype -Wl,--end-group)

install(TARGETS ${bin} DESTINATION bin)
