#include "sync_manager.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char* TAG = "SyncManager";

namespace SyncManager
{
    constexpr uint32_t BIT_NET = 1u << 0;
    constexpr uint32_t BIT_DEV = 1u << 1;
    constexpr uint32_t BIT_BLE = 1u << 2;
    constexpr uint32_t BIT_SOC = 1u << 3;
    constexpr uint32_t BIT_WEB = 1u << 4;
    constexpr uint32_t BIT_UDP = 1u << 5;
    constexpr uint32_t BIT_MQT = 1u << 6;
    constexpr uint32_t BIT_MTT = 1u << 7;
    constexpr uint32_t BIT_STO = 1u << 8;
    static constexpr uint32_t ALL_MASK =
        BIT_NET | BIT_DEV | BIT_SOC | BIT_WEB | BIT_UDP | BIT_MQT | BIT_MTT | BIT_STO | BIT_BLE;
    static uint32_t ready_mask = 0;
    static void onReady(void* domain, esp_event_base_t base, int32_t id, void* arg)
    {
        EventId eventId = static_cast<EventId>(id);
        ESP_LOGI(TAG, "Recebido evento base=%s id=%d", base ? (const char*)base : "(null)", (int)id);
        if (eventId == EventId::READY_ALL) return;
        if (eventId == EventId::NET_READY  ||
            eventId == EventId::DEV_READY  ||
            eventId == EventId::SOC_READY  ||
            eventId == EventId::WEB_READY  ||
            eventId == EventId::UDP_READY  ||
            eventId == EventId::MQT_READY  ||
            eventId == EventId::MTT_READY  ||
            eventId == EventId::STO_READY  ||
            eventId == EventId::BLE_READY)
        {
            switch (eventId)
            {
                case EventId::NET_READY:  ready_mask |= BIT_NET; break;
                case EventId::DEV_READY:  ready_mask |= BIT_DEV; break;
                case EventId::SOC_READY:  ready_mask |= BIT_SOC; break;
                case EventId::WEB_READY:  ready_mask |= BIT_WEB; break;
                case EventId::UDP_READY:  ready_mask |= BIT_UDP; break;
                case EventId::MQT_READY:  ready_mask |= BIT_MQT; break;
                case EventId::MTT_READY:  ready_mask |= BIT_MTT; break;
                case EventId::STO_READY:  ready_mask |= BIT_STO; break;
                case EventId::BLE_READY:  ready_mask |= BIT_BLE; break;
                default:
                    return;
            }
            ESP_LOGI(TAG, "Mask=0x%08lu", ready_mask);
        }
        if (ready_mask == ALL_MASK)
        {
            ESP_LOGI(TAG, "→ Todos os domínios prontos: publicando READY_ALL");
            EventBus::post(EventDomain::READY, EventId::READY_ALL);
            deinit();
        }
    }
    esp_err_t init()
    {
        EventBus::regHandler(EventDomain::READY, &onReady, nullptr);
        return ESP_OK;
    }
    esp_err_t deinit()
    {
        ESP_LOGW(TAG, "Encerrando SyncManager e removendo handlers...");
        EventBus::unregHandler(EventDomain::READY, &onReady);
        ESP_LOGI(TAG, "SyncManager fora da fila de evento");
        return ESP_OK;
    }
}