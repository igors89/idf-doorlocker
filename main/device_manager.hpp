#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "driver/touch_pad.h"
#include "touch_element/touch_button.h"
#include "event_bus.hpp"
#include "storage_manager.hpp"
#include "esp_mac.h"
#include <stdio.h>
#include <string.h>
// #include "driver/i2c.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_adc/adc_oneshot.h"
// #include "qrcodegen.h"


namespace DeviceManager
{
    esp_err_t init();
    // static void init_gpios();
    static void init_touch();
    static void init_buzzer();
    //buzzer
    static void buzzer_timer_cb(void*);
    static void touch_event_cb(touch_button_handle_t handle, touch_button_message_t *msg, void *arg);
    static void touch_task(void*);

    static void storage_event_task(void* arg);
    static void onStorageEvent(void*, esp_event_base_t, int32_t id, void* event_data);
    static void onReadyEvent(void*, esp_event_base_t, int32_t id, void*);
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*);
};