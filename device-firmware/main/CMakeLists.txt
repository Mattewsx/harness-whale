idf_component_register(
    SRCS "whale_app.c" "whale_wire.c" "whale_ble.c" "whale_ui.c" "whale_reply_cache.c" "whale_audio.c" "whale_settings.c" "whale_voice.c" "whale_voice_window.c" "whale_assets.c" "whale_asset_pack.c" "whale_ota.c" "whale_ota_core.c" "lv_font_cn_12.c" "lv_font_cn_14.c" "lv_font_cn_16.c" "lv_font_cn_18.c" "lv_font_cn_21.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_timer nvs_flash esp_audio_codec app_update esp_app_format esp_partition mbedtls
)

# Since v1.4 the pose, companion and cue data are not embedded in the
# application: they form an asset pack in the "assets" data partition so two
# OTA application slots fit in 8 MB. The pack is part of `idf.py flash` and of
# the merged full image; BLE OTA only replaces the application.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_build_get_property(python PYTHON)
    idf_build_get_property(build_dir BUILD_DIR)
    set(whale_pack "${build_dir}/whale-assets.bin")
    set(whale_pack_header "${CMAKE_CURRENT_BINARY_DIR}/whale_asset_pack_id.h")
    set(whale_pack_tool "${COMPONENT_DIR}/../tools/make_whale_asset_pack.py")
    set(whale_pack_inputs
        "${COMPONENT_DIR}/poses.rgb" "${COMPONENT_DIR}/companion.rgb" "${COMPONENT_DIR}/cues.pcm")
    partition_table_get_partition_info(whale_pack_size "--partition-name assets" "size")
    add_custom_command(
        OUTPUT "${whale_pack}" "${whale_pack_header}"
        COMMAND "${python}" "${whale_pack_tool}" --output "${whale_pack}"
                --header "${whale_pack_header}" --max-size "${whale_pack_size}"
                ${whale_pack_inputs}
        DEPENDS ${whale_pack_inputs} "${whale_pack_tool}"
        VERBATIM)
    add_custom_target(whale_asset_pack DEPENDS "${whale_pack}" "${whale_pack_header}")
    add_dependencies(${COMPONENT_LIB} whale_asset_pack)
    target_include_directories(${COMPONENT_LIB} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    esptool_py_flash_to_partition(flash "assets" "${whale_pack}")
endif()
