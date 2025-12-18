#include "storage_manager.hpp"
#include "storage.hpp"

static const char* TAG = "StorageManager";

namespace StorageManager {
    // Declaração dos ponteiros globais que serão alocados na PSRAM
    GlobalConfig* cfg = nullptr;
    IDConfig* id_cfg = nullptr;
    CredentialConfig* cd_cfg = nullptr;
    WifiScanCache* scanCache = nullptr;
    // Variáveis estáticas internas do módulo
    static QueueHandle_t s_storage_queue = nullptr;
    static SemaphoreHandle_t s_flash_mutex = nullptr;
    static std::unordered_map<std::string, Page*> pageMap;
    // --- Funções de Utilidade ---
    bool isBlankOrEmpty(const char* str) {
        if (str == nullptr || str[0] == '\0') {return true;}
        for (size_t i = 0; str[i] != '\0'; ++i) {if (!isspace(static_cast<unsigned char>(str[i]))) {return false;}}
        return true;
    }
    bool isWifiCacheValid() {
        if (!scanCache || !scanCache->networks_html_ptr) {return false;}
        if (scanCache->networks_html_len == 0 || scanCache->networks_html_ptr[0] == '\0') {return false;}
        time_t now = time(nullptr);
        time_t elapsed = now - scanCache->last_scan;
        if (elapsed > 300) {ESP_LOGD(TAG, "Cache WiFi expirado (%" PRId64 " segundos)", elapsed);return false;}
        return true;
    }
    void invalidateWifiCache() {
        if (!scanCache || !scanCache->networks_html_ptr) return;
        scanCache->networks_html_ptr[0] = '\0';
        scanCache->networks_html_len = 0;
        scanCache->last_scan = 0;
        ESP_LOGI(TAG, "Cache WiFi invalidado (conteúdo limpo).");
    }
    std::string replacePlaceholders(const std::string& content, const std::string& search, const std::string& replace) {
        std::string result = content;
        size_t pos = 0;
        while((pos=result.find(search,pos))!=std::string::npos){result.replace(pos,search.length(),replace);pos+=replace.length();}
        return result;
    }
    std::vector<std::string> splitString(const std::string& s, char delimiter) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, delimiter)) {
            tokens.push_back(token);
        }
        return tokens;
    }
    // --- Gerenciamento de Páginas Web ---
    void registerPage(const char* uri, Page* p) {
        pageMap[uri] = p;
        ESP_LOGI(TAG, "Página registrada: %s (%zu bytes, %s)", uri, p->size, p->mime.c_str());
    }
    const Page* getPage(const char* uri) {
        auto it = pageMap.find(uri);
        return (it != pageMap.end()) ? it->second : nullptr;
    }
    // --- Handlers de Eventos ---
    void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_IFOK) {
            ESP_LOGI(TAG, "Network IFOK recebido → checar SSID/password armazenados...");
            if (!isBlankOrEmpty(cd_cfg->ssid)) {
                EventBus::post(EventDomain::STORAGE, EventId::STO_SSIDOK);
            }
        }
    }
    // --- Enfileiramento de Requisições ---
    esp_err_t enqueueRequest(StorageCommand cmd, StorageStructType type, const void* data_to_copy, size_t data_len, RequestSave requester, EventId response_event_id) {
        void* data_buffer = nullptr;
        if (data_to_copy && data_len > 0) {
            data_buffer = heap_caps_malloc(data_len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if(data_buffer==nullptr){ESP_LOGE(TAG,"Falha ao alocar %zu bytes na PSRAM para requisição!",data_len);return ESP_ERR_NO_MEM;}
            memcpy(data_buffer, data_to_copy, data_len);
        }
        StorageRequest request;
        request.command = cmd;
        request.type = type;
        request.data_ptr = data_buffer;
        request.data_len = data_len;
        request.requester = requester;
        request.response_event_id = response_event_id;
        if (xQueueSend(s_storage_queue, &request, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG, "Falha ao enviar requisição para a fila de armazenamento!");
            if (data_buffer) {heap_caps_free(data_buffer);}
            return ESP_FAIL;
        }
        ESP_LOGD(TAG, "Requisição enfileirada: Comando=%d, Tipo=%d", static_cast<int>(cmd), static_cast<int>(type));
        return ESP_OK;
    }
    // --- Fila de Armazenamento (storage_task) ---
    static void storage_task(void* arg) {
        StorageRequest request;
        ESP_LOGI(TAG, "Storage Task iniciada.");
        while (true) {
            if (xQueueReceive(s_storage_queue, &request, portMAX_DELAY) == pdTRUE) {
                ESP_LOGI(TAG,"Requisição:Cmd=%d,Tp=%d,Ln=%zu",static_cast<int>(request.command),static_cast<int>(request.type),request.data_len);
                esp_err_t err = ESP_OK;
                if (xSemaphoreTake(s_flash_mutex, portMAX_DELAY) == pdTRUE) {
                    switch (request.command) {
                        case StorageCommand::SAVE: {
                            ESP_LOGI(TAG, "Processando SAVE para Tipo=%d", static_cast<int>(request.type));
                            switch (request.type) {
                                case StorageStructType::CONFIG_DATA: 
                                {
                                    if (request.data_ptr && request.data_len == sizeof(GlobalConfigDTO)) {
                                        memcpy(cfg, request.data_ptr, sizeof(GlobalConfigDTO));
                                        err=Storage::saveGlobalConfigFile(cfg);
                                        ESP_LOGI(TAG, "GlobalConfig salvo na flash. Status: %s", esp_err_to_name(err));
                                        if(request.response_event_id!=EventId::NONE){
                                            EventBus::post(EventDomain::STORAGE,request.response_event_id,&request.requester,sizeof(request.requester));
                                        }
                                    }else{ESP_LOGE(TAG, "SAVE CONFIG_DATA: Dados inválidos ou tamanho incorreto.");}
                                    break;
                                }
                                case StorageStructType::CREDENTIAL_DATA:
                                {
                                    if (request.data_ptr && request.data_len == sizeof(CredentialConfigDTO)) {
                                        memcpy(cd_cfg, request.data_ptr, sizeof(CredentialConfigDTO));
                                        Storage::saveCredentialConfigFile(cd_cfg);
                                        ESP_LOGI(TAG, "CredentialConfig salvo na flash. Status: %s", esp_err_to_name(err));
                                        if(request.response_event_id!=EventId::NONE){
                                            EventBus::post(EventDomain::STORAGE,request.response_event_id,&request.requester,sizeof(request.requester));
                                        }
                                    }else{ESP_LOGE(TAG, "SAVE CONFIG_DATA: Dados inválidos ou tamanho incorreto.");}
                                    break;
                                }
                            }
                            break;
                        }
                        case StorageCommand::DELETE: {
                            ESP_LOGI(TAG, "Processando DELETE para Tipo=%d (TODO)", static_cast<int>(request.type));
                            // TODO: Implementar lógica de DELETE
                            err = ESP_ERR_NOT_SUPPORTED;
                            break;
                        }
                    }
                    xSemaphoreGive(s_flash_mutex);
                } else {
                    ESP_LOGE(TAG, "Falha ao adquirir mutex da flash! Requisição não processada.");
                    // esp_err_t mutex_err = ESP_ERR_TIMEOUT; Não vou fazer nada.
                }
                if (request.data_ptr) {
                    heap_caps_free(request.data_ptr);
                    request.data_ptr = nullptr;
                }
            }
        }
    }
    // --- Inicialização do Módulo ---
    esp_err_t init() {
        ESP_LOGI(TAG, "Inicializando Storage Manager...");
        // 1. Alocação e inicialização dos objetos globais na PSRAM
        cfg = (GlobalConfig*)heap_caps_calloc(1, sizeof(GlobalConfig), MALLOC_CAP_SPIRAM);
        if (!cfg) { ESP_LOGE(TAG, "Falha ao alocar GlobalConfig na PSRAM"); return ESP_ERR_NO_MEM; }
        ESP_LOGI(TAG, "GlobalConfig alocado na PSRAM.");
        id_cfg = (IDConfig*)heap_caps_calloc(1, sizeof(IDConfig), MALLOC_CAP_SPIRAM);
        if (!id_cfg) { heap_caps_free(cfg); ESP_LOGE(TAG, "Falha ao alocar IDConfig na PSRAM"); return ESP_ERR_NO_MEM; }
        ESP_LOGI(TAG, "IDConfig alocado na PSRAM.");
        cd_cfg = (CredentialConfig*)heap_caps_calloc(1, sizeof(CredentialConfig), MALLOC_CAP_SPIRAM);
        if (!cd_cfg) { heap_caps_free(cfg); ESP_LOGE(TAG, "Falha ao alocar CredentialConfig na PSRAM"); return ESP_ERR_NO_MEM; }
        ESP_LOGI(TAG, "CredentialConfig alocado na PSRAM.");
        scanCache = (WifiScanCache*)heap_caps_calloc(1, sizeof(WifiScanCache), MALLOC_CAP_SPIRAM);
        if (!scanCache) { heap_caps_free(cfg); heap_caps_free(id_cfg); ESP_LOGE(TAG, "Falha ao alocar WifiScanCache na PSRAM"); return ESP_ERR_NO_MEM; }
        ESP_LOGI(TAG, "WifiScanCache alocado na PSRAM.");
        // 2. Alocação do buffer HTML dentro do WifiScanCache
        scanCache->networks_html_ptr = (char*)heap_caps_malloc(MAX_HTML_OPTIONS_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
        if (!scanCache->networks_html_ptr) {
            ESP_LOGE(TAG, "Falha ao alocar %d bytes para cache WiFi HTML na PSRAM", MAX_HTML_OPTIONS_BUFFER_SIZE);
            heap_caps_free(cfg); heap_caps_free(id_cfg); heap_caps_free(scanCache); scanCache = nullptr;
            return ESP_ERR_NO_MEM;
        }
        scanCache->networks_html_ptr[0] = '\0';
        ESP_LOGI(TAG, "Buffer HTML para WifiScanCache alocado na PSRAM.");
        // 3. Inicialização de IDConfig com MAC e ID do dispositivo (não persistido)
        uint8_t mac[6];
        esp_read_mac(mac, ESP_MAC_WIFI_STA);
        snprintf(id_cfg->mac, sizeof(id_cfg->mac), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        snprintf(id_cfg->id, sizeof(id_cfg->id), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ESP_LOGI(TAG, "IDConfig inicializado com MAC: %s, ID: %s", id_cfg->mac, id_cfg->id);
        // 4. Montagem do sistema de arquivos (LittleFS)
        ESP_LOGI(TAG, "Montando Storage físico (LittleFS)...");
        esp_err_t ret = Storage::init();
        if (ret != ESP_OK) { ESP_LOGE(TAG, "Falha ao inicializar Storage físico"); return ret; }
        ESP_LOGI(TAG, "Storage físico inicializado.");
        // 5. Criação da fila de requisições
        s_storage_queue = xQueueCreate(10, sizeof(StorageRequest));
        if (s_storage_queue == nullptr) { ESP_LOGE(TAG, "Falha ao criar fila de armazenamento!"); return ESP_FAIL; }
        ESP_LOGI(TAG, "Fila de armazenamento criada.");
        // 6. Criação do mutex para acesso à flash
        s_flash_mutex = xSemaphoreCreateMutex();
        if (s_flash_mutex == nullptr) { ESP_LOGE(TAG, "Falha ao criar mutex da flash!"); vQueueDelete(s_storage_queue); return ESP_FAIL; }
        ESP_LOGI(TAG, "Mutex da flash criado.");
        // 7. Criação da task de armazenamento
        BaseType_t xReturned = xTaskCreate(storage_task, "storage_task", 4096, nullptr, 5, nullptr);
        if (xReturned != pdPASS) {
            ESP_LOGE(TAG, "Falha ao criar task de storage!");
            vQueueDelete(s_storage_queue);
            vSemaphoreDelete(s_flash_mutex);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "Task de storage criada.");
        // 8. Registro do handler de eventos de rede
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        ESP_LOGI(TAG, "Handler de eventos de rede registrado.");
        // 9. Posta evento de ready
        EventBus::post(EventDomain::READY, EventId::STO_READY);
        ESP_LOGI(TAG, "→ STO_READY publicado");
        return ESP_OK;
    }
}
