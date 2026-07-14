cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED PROJECT_VER)
    execute_process(
        COMMAND git rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}"
        OUTPUT_VARIABLE D1L_GIT_SHORT_SHA
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(D1L_GIT_SHORT_SHA)
        set(PROJECT_VER "${D1L_GIT_SHORT_SHA}")
    endif()
endif()

set(SEEED_BSP_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/sensecap_indicator_esp32")
set(SEEED_BSP_TOUCH_PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/sensecap_indicator_touch_fix.patch")
set(SEEED_BSP_IDF55_COMPAT_PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/sensecap_indicator_idf55_compat.patch")
set(SEEED_BSP_TX_ORIGIN_PATCH "${CMAKE_CURRENT_LIST_DIR}/patches/sensecap_indicator_tx_origin.patch")
set(SEEED_BSP_PATCHES
    "${SEEED_BSP_TOUCH_PATCH}"
    "${SEEED_BSP_IDF55_COMPAT_PATCH}"
    "${SEEED_BSP_TX_ORIGIN_PATCH}"
)

foreach(SEEED_BSP_PATCH IN LISTS SEEED_BSP_PATCHES)
    get_filename_component(SEEED_BSP_PATCH_NAME "${SEEED_BSP_PATCH}" NAME)
    if(NOT EXISTS "${SEEED_BSP_PATCH}")
        message(FATAL_ERROR "Required Seeed BSP patch is missing: ${SEEED_BSP_PATCH_NAME}")
    endif()

    execute_process(
        COMMAND git apply --unidiff-zero --check --ignore-space-change "${SEEED_BSP_PATCH}"
        WORKING_DIRECTORY "${SEEED_BSP_DIR}"
        RESULT_VARIABLE SEEED_BSP_PATCH_APPLIES
        OUTPUT_QUIET
        ERROR_QUIET
    )
    if(SEEED_BSP_PATCH_APPLIES EQUAL 0)
        execute_process(
            COMMAND git apply --unidiff-zero --ignore-space-change "${SEEED_BSP_PATCH}"
            WORKING_DIRECTORY "${SEEED_BSP_DIR}"
            RESULT_VARIABLE SEEED_BSP_PATCH_RESULT
            OUTPUT_VARIABLE SEEED_BSP_PATCH_OUTPUT
            ERROR_VARIABLE SEEED_BSP_PATCH_ERROR
        )
        if(NOT SEEED_BSP_PATCH_RESULT EQUAL 0)
            message(FATAL_ERROR "Failed to apply Seeed BSP patch ${SEEED_BSP_PATCH_NAME}: ${SEEED_BSP_PATCH_ERROR}")
        endif()
        message(STATUS "Applied Seeed BSP patch: ${SEEED_BSP_PATCH_NAME}")
    else()
        execute_process(
            COMMAND git apply --unidiff-zero --reverse --check --ignore-space-change "${SEEED_BSP_PATCH}"
            WORKING_DIRECTORY "${SEEED_BSP_DIR}"
            RESULT_VARIABLE SEEED_BSP_PATCH_REVERSES
            OUTPUT_QUIET
            ERROR_QUIET
        )
        if(SEEED_BSP_PATCH_REVERSES EQUAL 0)
            message(STATUS "Seeed BSP patch already applied: ${SEEED_BSP_PATCH_NAME}")
        else()
            message(FATAL_ERROR "Seeed BSP patch ${SEEED_BSP_PATCH_NAME} is neither applicable nor already applied")
        endif()
    endif()
endforeach()

set(SEEED_COMPONENT_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party/sensecap_indicator_esp32/components")

set(EXTRA_COMPONENT_DIRS
    "${SEEED_COMPONENT_DIR}/bsp"
    "${SEEED_COMPONENT_DIR}/bus"
    "${SEEED_COMPONENT_DIR}/i2c_devices"
    "${SEEED_COMPONENT_DIR}/iot_button"
    "${SEEED_COMPONENT_DIR}/lora"
    "${SEEED_COMPONENT_DIR}/lvgl"
)

set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(meshcore_deskos_d1l)

foreach(seeed_component IN ITEMS
    bsp
    bus
    i2c_devices
    iot_button
    lora
    lvgl
)
    idf_component_get_property(seeed_component_lib ${seeed_component} COMPONENT_LIB)
    target_compile_options(${seeed_component_lib} PRIVATE
        "-Wno-error=format"
        "-Wno-error=maybe-uninitialized"
    )
endforeach()
