cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/third_party/sensecap_indicator_esp32/components"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(meshcore_deskos_d1l)

foreach(seeed_component IN ITEMS
    LoRaWAN
    bsp
    bus
    i2c_devices
    iot_button
    liblorahub
    lora
    lvgl
    radio_drivers
    smtc_ral
)
    idf_component_get_property(seeed_component_lib ${seeed_component} COMPONENT_LIB)
    target_compile_options(${seeed_component_lib} PRIVATE "-Wno-error=format")
endforeach()
