# set common files
set(common_files "../../common_files")
include_directories(${common_files})
set(common_files_src 
    ${common_files}/ai_base.cc
)

# set source_code
set(src main.cc hifigan.cc fastspeech1.cc fastspeech2.cc VoxCommon.cpp length_regulator.cpp)
set(all_src ${src} ${common_files_src})
set(bin tts_zh.elf)

include_directories(${PROJECT_SOURCE_DIR})
include_directories(.)

add_executable(${bin} ${all_src})
target_link_libraries(${bin} -Wl,--start-group rvv Nncase.Runtime.Native nncase.rt_modules.k230 functional_k230 sys ao ai -Wl,--end-group)

target_link_libraries(${bin} zh_frontend_lib)
install(TARGETS ${bin} DESTINATION bin)