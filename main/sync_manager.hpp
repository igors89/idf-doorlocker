#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "esp_err.h"

namespace SyncManager
{
    esp_err_t init();
    esp_err_t deinit();
}