idf_component_register(
    SRCS
        "../overlays/meshcore_ed25519_defined/fe.c"
        "../overlays/meshcore_ed25519_defined/ge.c"
        "../third_party/MeshCore/lib/ed25519/key_exchange.c"
        "../third_party/MeshCore/lib/ed25519/keypair.c"
        "../overlays/meshcore_ed25519_defined/sc.c"
        "../third_party/MeshCore/lib/ed25519/sha512.c"
        "../third_party/MeshCore/lib/ed25519/sign.c"
        "../third_party/MeshCore/lib/ed25519/verify.c"
        "app_main.c"
        "app/app_model.c"
        "app/settings_model.c"
        "comms/connectivity_manager.c"
        "comms/companion_3byte.c"
        "comms/usb_console.c"
        "diagnostics/crash_log.c"
        "diagnostics/health_monitor.c"
        "hal/backlight.c"
        "hal/indicator_board.c"
        "hal/indicator_pins.c"
        "hal/rp2040_bridge.c"
        "hal/rp2040_file_reply.c"
        "hal/rp2040_sd_reply.c"
        "hal/sx1262_indicator.c"
        "map/map_math.c"
        "map/map_point_projection.c"
        "map/map_png_decoder.c"
        "map/map_view_service.c"
        "mesh/advert_data.c"
        "mesh/channel_store.c"
        "mesh/contact_store.c"
        "mesh/dm_delivery_state.c"
        "mesh/contact_uri.c"
        "mesh/dm_store.c"
        "mesh/mesh_inspector.c"
        "mesh/message_store.c"
        "mesh/node_store.c"
        "mesh/packet_log.c"
        "mesh/read_state.c"
        "mesh/route_store.c"
        "mesh/route_store_worker.c"
        "mesh/meshcore_service.c"
        "mesh/meshcore_wire.c"
        "mesh/meshcore_radio_profile.c"
        "storage/retained_blob_store.c"
        "storage/export_store.c"
        "storage/map_tile_store.c"
        "storage/storage_status.c"
        "storage/storage_status_policy.c"
        "ui/ui_chrome.c"
        "ui/ui_home.c"
        "ui/ui_keyboard.c"
        "ui/ui_map.c"
        "ui/ui_messages.c"
        "ui/ui_modal.c"
        "ui/ui_navigation.c"
        "ui/ui_nodes.c"
        "ui/ui_phase1.c"
        "ui/ui_screen.c"
        "ui/ui_settings.c"
    INCLUDE_DIRS
        "."
        "app"
        "comms"
        "diagnostics"
        "hal"
        "map"
        "mesh"
        "storage"
        "../third_party/MeshCore/lib/ed25519"
        "ui"
    REQUIRES
        bsp
        driver
        esp_timer
        esp_system
        esp_event
        esp_http_client
        esp_netif
        esp_wifi
        heap
        i2c_devices
        lora
        lvgl
        mbedtls
        nvs_flash
)

set(D1L_BUILD_GIT_COMMIT "$ENV{GITHUB_SHA}")
string(LENGTH "${D1L_BUILD_GIT_COMMIT}" D1L_BUILD_GIT_COMMIT_LENGTH)
if(NOT D1L_BUILD_GIT_COMMIT_LENGTH EQUAL 40 OR
   NOT D1L_BUILD_GIT_COMMIT MATCHES "^[0-9a-fA-F]+$")
    execute_process(
        COMMAND git rev-parse HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/.."
        OUTPUT_VARIABLE D1L_BUILD_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
string(LENGTH "${D1L_BUILD_GIT_COMMIT}" D1L_BUILD_GIT_COMMIT_LENGTH)
if(NOT D1L_BUILD_GIT_COMMIT_LENGTH EQUAL 40 OR
   NOT D1L_BUILD_GIT_COMMIT MATCHES "^[0-9a-fA-F]+$")
    set(D1L_BUILD_GIT_COMMIT "unknown")
else()
    string(TOLOWER "${D1L_BUILD_GIT_COMMIT}" D1L_BUILD_GIT_COMMIT)
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    MESHCORE_CA_DESK_MODE=1
    D1L_BUILD_GIT_COMMIT="${D1L_BUILD_GIT_COMMIT}"
)
