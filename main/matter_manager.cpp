#include "matter_manager.hpp"

static const char* TAG = "MatterManager";
namespace MatterManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Matter...");
        EventBus::post(EventDomain::READY, EventId::MTT_READY);
        ESP_LOGI(TAG, "→ MTT_READY publicado");
        return ESP_OK;
    }
}