# Shared UART2 VAXP 1.0 result transport for camera AI applications.
get_filename_component(DSHANPI_AI_ROOT
    "${PROJECT_SOURCE_DIR}/../.." ABSOLUTE)
get_filename_component(DSHANPI_REPO_ROOT
    "${DSHANPI_AI_ROOT}/../../.." ABSOLUTE)

if(DEFINED ENV{SDK_RTSMART_BUILD_DIR} AND
   NOT "$ENV{SDK_RTSMART_BUILD_DIR}" STREQUAL "")
    set(DSHANPI_RTSMART_HAL "$ENV{SDK_RTSMART_BUILD_DIR}/libs/rtsmart_hal")
else()
    set(DSHANPI_RTSMART_HAL
        "${DSHANPI_REPO_ROOT}/output/k230_canmv_dongshanpi_defconfig/rtsmart/libs/rtsmart_hal")
endif()

set(VAXP_AI_SOURCES
    ${DSHANPI_AI_ROOT}/uart/vaxp_ai_stream.c
    ${DSHANPI_AI_ROOT}/uart/vaxp_lab.c
    ${DSHANPI_AI_ROOT}/uart/uart_lab.c
    ${DSHANPI_AI_ROOT}/system/system_settings.c)

include_directories(
    ${DSHANPI_AI_ROOT}/uart
    ${DSHANPI_AI_ROOT}/system
    ${DSHANPI_AI_ROOT}/third_party/vaxp/include
    ${DSHANPI_RTSMART_HAL}/include
    ${DSHANPI_REPO_ROOT}/src/rtsmart/libs/rtsmart_hal/drivers/uart
    ${DSHANPI_REPO_ROOT}/src/rtsmart/libs/rtsmart_hal/drivers/fpioa)

link_directories(${DSHANPI_RTSMART_HAL}/lib)
link_libraries(uart fpioa utils)
