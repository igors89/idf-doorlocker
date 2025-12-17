#include "ble_manager.hpp"

static const char* TAG = "BleManager";
namespace BleManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando Ble...");
        EventBus::post(EventDomain::READY, EventId::BLE_READY);
        ESP_LOGI(TAG, "→ BLE_READY publicado");
        return ESP_OK;
    }
}