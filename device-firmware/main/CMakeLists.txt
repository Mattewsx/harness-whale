idf_component_register(
    SRCS "whale_app.c" "whale_wire.c" "whale_ble.c" "whale_ui.c" "whale_reply_cache.c" "whale_audio.c" "whale_voice.c" "whale_voice_window.c" "lv_font_cn_12.c" "lv_font_cn_14.c" "lv_font_cn_16.c" "lv_font_cn_18.c" "lv_font_cn_21.c"
    INCLUDE_DIRS "."
    REQUIRES bsp bt esp_timer nvs_flash esp_audio_codec
    EMBED_FILES "poses.rgb"
)
