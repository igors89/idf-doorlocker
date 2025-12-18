#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "storage_manager.hpp"

namespace MqttManager
{
    esp_err_t init();
    void stop_mqtt();
    int publish(const char* topic, const char* data, int len, int qos, int retain);
}