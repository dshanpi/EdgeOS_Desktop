# set common files
set(common_files "../common_files")
include_directories(${common_files})
set(common_files_src
    ${common_files}/ai_base.cc
)

# set source_code
set(src main.cc kws.cc fft.cc feature_pipeline.cc pcm_data.cc wav_ctrl.cc)
set(all_src ${src} ${common_files_src})
set(bin kws.elf)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(.)

add_executable(${bin} ${all_src})
target_link_libraries(${bin} -Wl,--start-group rvv Nncase.Runtime.Native nncase.rt_modules.k230 functional_k230 sys vicap vb isp vo connector sensor atomic gsdma ai ao -Wl,--end-group)

target_link_libraries(${bin}  -Wl,--start-group opencv_imgcodecs opencv_imgproc opencv_core zlib libjpeg-turbo libopenjp2 libpng libtiff libwebp csi_cv -Wl,--end-group)

install(TARGETS ${bin} DESTINATION bin)