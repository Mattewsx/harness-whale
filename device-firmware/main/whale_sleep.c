#include "whale_sleep.h"
#include "bsp_audio.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "whale_ble.h"

#define WHALE_SLEEP_MAGIC 0x57484C53UL
static const char *TAG = "whale_sleep";
static RTC_DATA_ATTR uint32_t s_magic, s_cycles, s_sleeps;

static void warn(const char *step, esp_err_t error) {
    if (error != ESP_OK)
        ESP_LOGW(TAG, "sleep continues: %s failed: %s", step, esp_err_to_name(error));
}

whale_sleep_info_t whale_sleep_boot_info(void) {
    whale_sleep_info_t info = {.boot = WHALE_BOOT_POWER};
    if (s_magic != WHALE_SLEEP_MAGIC || esp_reset_reason() != ESP_RST_DEEPSLEEP) {
        s_magic = WHALE_SLEEP_MAGIC;
        s_cycles = s_sleeps = 0;
        return info;
    }
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    info.boot = cause == ESP_SLEEP_WAKEUP_TIMER ? WHALE_BOOT_TIMER : WHALE_BOOT_KEY;
    info.cycles = info.boot == WHALE_BOOT_TIMER ? s_cycles : 0;
    info.sleeps = s_sleeps;
    return info;
}

const char *whale_sleep_boot_name(whale_boot_t boot) {
    return boot == WHALE_BOOT_TIMER ? "timer" : boot == WHALE_BOOT_KEY ? "key" : "power";
}

bool whale_sleep_enter(uint32_t timer_ms, uint32_t cycles) {
    /* The shared key pin is also the wake pin: a held key would wake us at
     * once, so refuse while the buttons still work. */
    int mv = bsp_button_read_mv();
    if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) {
        ESP_LOGW(TAG, "key held (%d mV), not sleeping", mv);
        return false;
    }
    if (!whale_ble_stop_for_sleep()) {
        ESP_LOGW(TAG, "BLE busy, not sleeping");
        return false;
    }
    ESP_LOGI(TAG, "offline sleep #%lu timer=%lums cycles=%lu", (unsigned long)(s_sleeps + 1),
             (unsigned long)timer_ms, (unsigned long)cycles);
    vTaskDelay(pdMS_TO_TICKS(20)); /* let the log line drain */

    /* From here on everything is terminal: any failure restarts. */
    if (timer_ms && esp_sleep_enable_timer_wakeup((uint64_t)timer_ms * 1000ULL) != ESP_OK)
        esp_restart();
    int level = 0;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK)
        esp_restart();
    if (level != 1)
        ESP_LOGW(TAG, "key pressed while preparing; sleep may return at once");
    warn("GPIO wake arm",
         esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW));
    /* The CW2017 stays active so it keeps counting charge across sleeps; its
     * SOC after each wake stays continuous for battery-life measurement. */
    warn("ES8311 suspend", bsp_audio_sleep());
    warn("I2S pin release", bsp_audio_prepare_deep_sleep());
    warn("shared I2C pin release", bsp_i2c_prepare_deep_sleep());
    if (!bsp_lvgl_lock(1000))
        esp_restart();
    warn("ST7789 suspend", bsp_display_prepare_deep_sleep());

    s_magic = WHALE_SLEEP_MAGIC;
    s_cycles = cycles;
    s_sleeps++;
    esp_deep_sleep_start();
    esp_restart();
    return false;
}
