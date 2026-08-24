cmake_minimum_required(VERSION 3.2)
project(uvc_camera C CXX)
set(CMAKE_EXE_LINKER_FLAGS "-T ${PROJECT_SOURCE_DIR}/../face_studio/cmake/link.lds --static")
set(mpp $ENV{MPP_SRC_DIR})
set(uvc_example ${PROJECT_SOURCE_DIR}/../../../../rtsmart/examples/ai/uvc_face_detection/src)
include_directories(${mpp}/userapps/api ${mpp}/include ${mpp}/include/comm
    ${mpp}/userapps/sample/sample_vo ${uvc_example} ../face_studio/src
    ../../../../rtsmart/libs/rtsmart_hal/utils)
link_directories(${mpp}/userapps/lib)
add_executable(uvc_camera.elf
    src/uvc_test.c
    ../../../../rtsmart/libs/rtsmart_hal/utils/hal_rvv_ops.c
    ../face_studio/src/drv_touch.c)
target_compile_definitions(uvc_camera.elf PRIVATE DSHANPI_UVC_APP=1)
target_link_libraries(uvc_camera.elf -Wl,--start-group framework uvc sys vdec vb vo
    connector atomic gsdma -Wl,--end-group)
install(TARGETS uvc_camera.elf DESTINATION bin)
