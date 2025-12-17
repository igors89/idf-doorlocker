#include "mqtt_manager.hpp"

static const char* TAG = "MqttManager";
namespace MqttManager {
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando MQTT...");
        EventBus::post(EventDomain::READY, EventId::MQT_READY);
        ESP_LOGI(TAG, "→ MQT_READY publicado");
        return ESP_OK;
    }
}