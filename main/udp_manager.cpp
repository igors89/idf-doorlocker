#include "udp_manager.hpp"

static const char* TAG = "UdpManager";
namespace UdpManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando UDP...");
        EventBus::post(EventDomain::READY, EventId::UDP_READY);
        ESP_LOGI(TAG, "→ UDP_READY publicado");
        return ESP_OK;
    }
}