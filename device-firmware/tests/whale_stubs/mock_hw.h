#pragma once
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef void *QueueHandle_t;
typedef int bsp_btn_t;
typedef int bsp_btn_ev_t;
enum { BSP_BTN_UP, BSP_BTN_DOWN, BSP_BTN_OK };
enum { BSP_BTN_PRESS, BSP_BTN_CLICK, BSP_BTN_DOUBLE, BSP_BTN_LONG, BSP_BTN_RELEASE };
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERROR_CHECK(x) assert((x) == 0)
#define ESP_LOGI(...) (void)0
#define ESP_LOGW(...) (void)0
#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
void *xQueueCreate(int, int);
int xQueueReceive(void *, void *, int);
int xQueueSend(void *, const void *, int);
int bsp_battery_init(void);
int bsp_battery_soc(void);
int bsp_display_init(void);
void *bsp_lvgl_init(void);
bool bsp_lvgl_lock(int);
void bsp_lvgl_unlock(void);
void bsp_display_backlight(int);
int bsp_button_init(void (*f)(int, int, void *), void *);
void xTaskCreate(void (*f)(void *), const char *, int, void *, int, void *);
void vTaskDelay(int);
int nvs_flash_init(void);
int64_t esp_timer_get_time(void);
uint32_t esp_get_minimum_free_heap_size(void);
uint32_t esp_get_free_heap_size(void);
unsigned uxTaskGetStackHighWaterMark(void *task);

void esp_fill_random(void *p, size_t n);
