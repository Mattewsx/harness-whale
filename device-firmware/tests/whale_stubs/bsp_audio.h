#pragma once
#include <stddef.h>
#include <stdint.h>
#include "mock_hw.h"
typedef int esp_err_t;
esp_err_t bsp_audio_init(void);
esp_err_t bsp_audio_wake(void);
esp_err_t bsp_audio_sleep(void);
esp_err_t bsp_audio_set_format(uint32_t hz, uint8_t bits, uint8_t channels);
void bsp_audio_set_volume(uint8_t volume);
esp_err_t bsp_audio_write(const void *data, size_t size);
esp_err_t bsp_audio_read(void *data, size_t size);
