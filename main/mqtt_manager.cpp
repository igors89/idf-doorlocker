#include "mqtt_manager.hpp"

static const char* TAG = "MqttManager";
namespace MqttManager {

    static esp_mqtt_client_handle_t s_client = nullptr;
    static int s_retry_count = 0;
    static constexpr int MAX_RETRY = 5;
    static bool s_is_started = false;
    static TimerHandle_t s_retry_timer = nullptr;

    static void start_mqtt();

    static void multicast_timer_cb(TimerHandle_t xTimer) {
        ESP_LOGW(TAG, "Timer de recuperação expirou. Tentando reconectar ao broker...");
        start_mqtt();
    }

    // Callback interno da biblioteca MQTT do ESP-IDF
    static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
        esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;

        switch((esp_mqtt_event_id_t)event_id){
            case MQTT_EVENT_CONNECTED:
                ESP_LOGI(TAG, "MQTT Conectado ao Broker!");
                s_retry_count = 0;
                
                // Se o timer estiver rodando (caso tenha conectado por sorte antes do timer), paramos ele
                if(s_retry_timer && xTimerIsTimerActive(s_retry_timer)){
                     xTimerStop(s_retry_timer, 0);
                }

                // Se conecta e já se inscreve no tópico de comando do dispositivo
                char topic_sub[64];
                snprintf(topic_sub, sizeof(topic_sub), "DSP/%s", StorageManager::id_cfg->id);
                esp_mqtt_client_subscribe(s_client, topic_sub, 0);
                ESP_LOGI(TAG, "Inscrito no tópico: %s", topic_sub);
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_mqtt_client_publish(s_client, "SRV/TecladoESP", StorageManager::id_cfg->id, 0, 0, 0);
                ESP_LOGI(TAG, "ID publicado em SRV/TecladoESP");
                break;

            case MQTT_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "MQTT Desconectado.");
                if (s_retry_count < MAX_RETRY) {
                    s_retry_count++;
                    ESP_LOGW(TAG, "Tentando reconectar... (%d/%d)", s_retry_count, MAX_RETRY);
                } else {
                    // Se falhou muitas vezes, assume que o Broker mudou de IP ou caiu
                    ESP_LOGE(TAG, "Falha crítica após %d tentativas. Parando MQTT.", s_retry_count);
                    
                    // Posta evento pedindo parada interna
                    EventBus::post(EventDomain::MQTT, EventId::MQT_STOP_REQ);
                }
                break;

            case MQTT_EVENT_DATA:
                ESP_LOGI(TAG, "Mensagem Recebida!");
                ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
                ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);
                
                // TODO: Aqui futuramente você pode adicionar o parser de comandos
                // Ex: Se receber comando "OPEN", chamar DeviceManager::unlock()
                break;

            case MQTT_EVENT_ERROR:
                if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                    ESP_LOGE(TAG, "Erro de transporte TCP (Socket errno=%d)", event->error_handle->esp_transport_sock_errno);
                } else {
                    ESP_LOGE(TAG, "Erro MQTT genérico");
                }
                break;

            default:
                break;
        }
    }

    static void start_mqtt(){
        s_retry_count = 0;
        if(s_is_started){
            ESP_LOGW(TAG, "Cliente MQTT já está rodando.");
            return;
        }

        esp_mqtt_client_config_t mqtt_cfg = {};
        
        // CORREÇÃO: cfg é ponteiro
        mqtt_cfg.broker.address.hostname = "server.ia.srv.br";
        mqtt_cfg.broker.address.port = 1883; 
        mqtt_cfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;
        
        mqtt_cfg.credentials.username = "igramosquitto";
        mqtt_cfg.credentials.authentication.password = "mosquittopass";
        
        // CORREÇÃO: id está em id_cfg->id
        mqtt_cfg.credentials.client_id = StorageManager::id_cfg->id;
        
        mqtt_cfg.session.keepalive = 60;

        s_client = esp_mqtt_client_init(&mqtt_cfg);
        if(s_client){
            esp_mqtt_client_register_event(s_client, MQTT_EVENT_ANY, mqtt_event_handler, NULL);
            if(esp_mqtt_client_start(s_client) == ESP_OK){
                s_is_started = true;
                ESP_LOGI(TAG, "Cliente MQTT iniciado.");
            } else {
                ESP_LOGE(TAG, "Falha ao iniciar cliente MQTT.");
            }
        } else {
            ESP_LOGE(TAG, "Falha crítica ao alocar memória para MQTT.");
        }
    }

    void stop_mqtt() {
        if (!s_is_started || !s_client) return;
        
        ESP_LOGI(TAG, "Parando serviço MQTT...");
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = nullptr;
        s_is_started = false;
    }

    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*) {
        EventId evt = static_cast<EventId>(id);

        if(evt == EventId::NET_STAGOTIP){
            if(s_retry_timer) xTimerStop(s_retry_timer, 0);
            ESP_LOGI(TAG, "Rede OK e IP do Broker conhecido. Iniciando MQTT...");
            start_mqtt();
        }
        if(evt == EventId::NET_STADISCONNECTED){
            stop_mqtt(); 
            if(s_retry_timer) xTimerStop(s_retry_timer, 0);
        }
    }

    static void onEvent(void*, esp_event_base_t base, int32_t id, void*){
        EventId evt = static_cast<EventId>(id);

        if(evt == EventId::MQT_STOP_REQ){
            ESP_LOGW(TAG, "Solicitação de parada (MQT_STOP_REQ).");
            stop_mqtt();
        }
    }

    int publish(const char* topic, const char* data, int len, int qos, int retain) {
        if (!s_is_started || !s_client) {
            ESP_LOGW(TAG, "Tentativa de publicar sem conexão MQTT.");
            return -1;
        }
        return esp_mqtt_client_publish(s_client, topic, data, len, qos, retain);
    }
    
    esp_err_t init(){
        ESP_LOGI(TAG, "Inicializando MQTT...");

        s_retry_timer = xTimerCreate("mqtt_disc_tmr", pdMS_TO_TICKS(300000), pdTRUE, nullptr, multicast_timer_cb);
        if (!s_retry_timer) {
            ESP_LOGE(TAG, "Falha ao criar timer de descoberta!");
        }

        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        EventBus::regHandler(EventDomain::MQTT, &onEvent, nullptr);
        
        EventBus::post(EventDomain::READY, EventId::MQT_READY);
        ESP_LOGI(TAG, "→ MQT_READY publicado");
        return ESP_OK;
    }
}