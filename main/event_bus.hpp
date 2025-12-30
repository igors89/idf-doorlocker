#pragma once
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include "unordered_map"

enum class EventDomain : uint8_t {NETWORK,READY,DEVICE,SOCKET,WEB,UDP,MQTT,MATTER,STORAGE,BLE};
enum class EventId : int32_t {
    NONE=0,READY_ALL,
    NET_READY,NET_APDISCONNECTED,NET_APCONNECTED,NET_IFOK,NET_TEST,NET_LISTQRY,NET_LISTOK,NET_LISTFAIL,NET_APCLIGOTIP,
    NET_STADISCONNECTED,NET_STACONNECTED,NET_STASTARTED,NET_STAGOTIP,NET_APCLICONNECTED,NET_APCLIDISCONNECTED,NET_STASTOPPED,
    NET_TESTFAILTRY,NET_TESTFAILREVERT,NET_SUSPCLIENT, NET_UPDATE_AP_CONFIG,
    DEV_READY,DEV_STARTED,
    SOC_READY,SOC_STARTED,
    WEB_READY,WEB_STARTED,
    UDP_READY,
    MQT_READY,
    MTT_READY, MQT_STOP_REQ,
    STO_READY,STO_SSIDOK,STO_QUERY,STO_UPDATE,STO_CONFIGSAVED,STO_CREDENTIALSAVED,STO_DEVICESAVED,
    BLE_READY
};
struct EventPayload {void* data; size_t size;};
namespace EventBus {
    esp_err_t init();
    esp_err_t post(EventDomain domain, EventId id, void* data = nullptr, size_t size = 0, TickType_t timeout = 0);
    esp_err_t regHandler(EventDomain domain, esp_event_handler_t handler, void* arg = nullptr);
    esp_err_t unregHandler(EventDomain domain, esp_event_handler_t handler);
}