idf_component_register(
    SRCS "whale_app.c" "whale_wire.c" "whale_ble.c" "whale_ui.c" "whale_reply_cache.c" "whale_audio.c" "whale_settings.c" "whale_voice.c" "whale_voice_window.c" "whale_power.c" "whale_sleep.c" "whale_battery_log.c" "whale_clock.c" "lv_font_cn_12.c" "lv_font_cn_14.c" "lv_font_cn_16.c" "lv_font_cn_18.c" "lv_font_cn_21.c" "lv_font_cn_64.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_timer nvs_flash esp_audio_codec
    EMBED_FILES "poses.rgb" "companion.rgb" "room.rgb" "cues.pcm"
)
# Bench build with short power thresholds: idf.py -D WHALE_POWER_TEST=1 build
if(WHALE_POWER_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE WHALE_POWER_TEST=1)
endif()
