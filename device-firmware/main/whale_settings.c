#include "whale_settings.h"
#include "nvs.h"
whale_sound_settings_t whale_sound_load(void) {
    uint8_t data[2];
    if (whale_blob_load("sound_v2", data, sizeof data) && data[0] <= 1 &&
        data[1] <= 100 && data[1] % 5 == 0)
        return (whale_sound_settings_t){data[0], data[1]};

    /* Old installations stored only the mode and played at a fixed level. */
    uint8_t mode = 1;
    nvs_handle_t h;
    if (nvs_open("whale", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, "sound_v1", &mode) != ESP_OK || mode > 1)
            mode = 1;
        nvs_close(h);
    }
    return (whale_sound_settings_t){mode, mode ? 80 : 85};
}
bool whale_sound_save(whale_sound_settings_t sound) {
    const uint8_t data[] = {sound.mode, sound.volume};
    return sound.mode <= 1 && sound.volume <= 100 && sound.volume % 5 == 0 &&
           whale_blob_save("sound_v2", data, sizeof data);
}
uint8_t whale_standby_load(void) {
    nvs_handle_t h;
    uint8_t mode = 0;
    if (nvs_open("whale", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, "standby_v1", &mode) != ESP_OK || mode > 1)
            mode = 0;
        nvs_close(h);
    }
    return mode;
}
bool whale_standby_save(uint8_t mode) {
    nvs_handle_t h;
    if (mode > 1 || nvs_open("whale", NVS_READWRITE, &h) != ESP_OK)
        return false;
    esp_err_t err = nvs_set_u8(h, "standby_v1", mode);
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}
bool whale_blob_load(const char *key, void *data, size_t size) {
    nvs_handle_t h;
    size_t length = size;
    if (nvs_open("whale", NVS_READONLY, &h) != ESP_OK)
        return false;
    esp_err_t err = nvs_get_blob(h, key, data, &length);
    nvs_close(h);
    return err == ESP_OK && length == size;
}
bool whale_blob_save(const char *key, const void *data, size_t size) {
    nvs_handle_t h;
    if (nvs_open("whale", NVS_READWRITE, &h) != ESP_OK)
        return false;
    esp_err_t err = nvs_set_blob(h, key, data, size);
    if (err == ESP_OK)
        err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}
