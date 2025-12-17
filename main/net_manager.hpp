#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "event_bus.hpp"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_mac.h"
#include "dns_server.h"
#include "lwip/lwip_napt.h"
#include "lwip/netif.h"
#include "lwip/esp_netif_net_stack.h"
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstddef>
#include "esp_heap_caps.h"
#include <algorithm>
#include "storage_manager.hpp"
#include "freertos/timers.h"


namespace NetManager
{
    esp_err_t init();
    static esp_err_t startAP();
    static void chage_timer_ap(TimerHandle_t xTimer);
    std::vector<std::string> fetchFirmwareList();
}