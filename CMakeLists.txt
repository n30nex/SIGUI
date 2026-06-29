cmake_minimum_required(VERSION 3.16)

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
