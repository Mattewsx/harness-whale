#include "whale_settings.h"
#include "nvs.h"
uint8_t whale_sound_load(void) {
    nvs_handle_t h;
    uint8_t mode = 1;
    if (nvs_open("whale", NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u8(h, "sound_v1", &mode) != ESP_OK || mode > 1)
            mode = 1;
        nvs_close(h);
    }
    return mode;
}
bool whale_sound_save(uint8_t mode) {
    nvs_handle_t h;
    if (mode > 1 || nvs_open("whale", NVS_READWRITE, &h) != ESP_OK)
        return false;
    esp_err_t err = nvs_set_u8(h, "sound_v1", mode);
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
