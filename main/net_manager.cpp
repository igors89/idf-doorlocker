#include "net_manager.hpp"

static const char* TAG = "NetManager";

namespace NetManager
{
    static bool s_isTestConnection = false;
    static bool s_isAlreadyTryConnection = false;
    static int s_retry_count = 0;
    static constexpr int MAX_RETRY = 5;
    static esp_netif_t* netif_ap = nullptr;
    static esp_netif_t* netif_sta = nullptr;
    static bool s_scan_in_progress = false;
    static int s_requesting_fd = -1;
    static testSSID local_test_data;
    TimerHandle_t blocked_aid_timer = nullptr;
    static TimerHandle_t ap_change_timer = nullptr;
    uint32_t current_blocked_aid = 0;
    static esp_err_t connectSTA(const char* testSsid = nullptr, const char* testPass = nullptr, bool isTest = false)
    {
        s_isTestConnection = isTest;
        if (!s_isTestConnection){s_isAlreadyTryConnection=true;}
        else {ESP_LOGI(TAG, "Modo TESTE: esta conexão não será salva.");}
        const char* ssidToUse = testSsid ? testSsid : StorageManager::cd_cfg->ssid;
        const char* passToUse = testPass ? testPass : StorageManager::cd_cfg->password;
        ESP_LOGI(TAG, "%s conexão STA... (SSID: %s)",s_isTestConnection ? "Testando" : "Iniciando", ssidToUse);
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            ESP_LOGW(TAG, "STA já está conectada a '%s'. Desconectando antes de tentar nova conexão.", (char*)ap_info.ssid);
            esp_err_t disconnect_ret = esp_wifi_disconnect();
            if(disconnect_ret!=ESP_OK){ESP_LOGE(TAG,"Falha ao desconectar STA existente: %s",esp_err_to_name(disconnect_ret));}
            else{ESP_LOGI(TAG,"STA desconectada com sucesso.");}
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        wifi_config_t sta_cfg{};
        strncpy((char*)sta_cfg.sta.ssid, ssidToUse, sizeof(sta_cfg.sta.ssid) - 1);
        sta_cfg.sta.ssid[sizeof(sta_cfg.sta.ssid) - 1] = '\0';
        strncpy((char*)sta_cfg.sta.password, passToUse, sizeof(sta_cfg.sta.password) - 1);
        sta_cfg.sta.password[sizeof(sta_cfg.sta.password) - 1] = '\0';
        sta_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        sta_cfg.sta.pmf_cfg.capable = true;
        sta_cfg.sta.pmf_cfg.required = false;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA,&sta_cfg));
        esp_err_t ret = esp_wifi_connect();
        if (ret == ESP_OK) ESP_LOGI(TAG, "STA %s iniciada, aguardando conexão...", s_isTestConnection ? "de teste" : "real");
        else ESP_LOGE(TAG, "Falha ao iniciar STA (%s): %s", s_isTestConnection ? "teste" : "real", esp_err_to_name(ret));
        return ret;
    }
    static void unblock_aid_timer_callback(TimerHandle_t xTimer) {
        ESP_LOGI(TAG, "Timer de bloqueio expirou. Desbloqueando AID.");
        current_blocked_aid = 0;
        ESP_LOGI(TAG, "AID desbloqueado.");
    }
    void blockClientAID() {
        if (blocked_aid_timer == nullptr) {ESP_LOGE(TAG,"Timer de bloqueio não inicializado!");return;}
        ESP_LOGW(TAG, "AID %" PRIu32" bloqueado por 30s.",current_blocked_aid);
        if (xTimerReset(blocked_aid_timer, 0) != pdPASS) {ESP_LOGE(TAG, "Falha ao iniciar/resetar o timer de bloqueio!");}
        else {ESP_LOGI(TAG, "Timer de bloqueio iniciado.");}
    }
    static void chage_timer_ap(TimerHandle_t xTimer){
        ESP_LOGI(TAG, "[TIMER] 60s passaram. Alterando dados do AP...");
        wifi_config_t ap_cfg{};
        strncpy((char*)ap_cfg.ap.ssid, StorageManager::cfg->central_name, sizeof(ap_cfg.ap.ssid) - 1);
        ap_cfg.ap.ssid[sizeof(ap_cfg.ap.ssid)-1]='\0';
        ap_cfg.ap.ssid_len=strlen((char*)ap_cfg.ap.ssid);
        ap_cfg.ap.channel=1;
        ap_cfg.ap.max_connection=4;
        ap_cfg.ap.authmode=WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP,&ap_cfg));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        vTaskDelay(pdMS_TO_TICKS(100));
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_LOGI(TAG,"AP atualizado para SSID: %s",ap_cfg.ap.ssid);
    }
    static void onEventReadyBus(void*,esp_event_base_t base,int32_t id,void*)
    {
        if(static_cast<EventId>(id)==EventId::READY_ALL){
            if (startAP() != ESP_OK) {
                ESP_LOGE(TAG, "Falha ao criar AP após READY_ALL.");
            } else {
                vTaskDelay(pdMS_TO_TICKS(300));
                EventBus::post(EventDomain::NETWORK, EventId::NET_IFOK);
                ESP_LOGI(TAG, "→ NET_IFOK publicado após AP iniciar.");
            }
        }
    }
    static void onEventNetworkBus(void*,esp_event_base_t base,int32_t id,void* data)
    {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::NET_LISTQRY) {
            if (data) {memcpy(&s_requesting_fd, data, sizeof(int));ESP_LOGI(TAG, "Pedido de lista WiFi recebido (fd=%d)", s_requesting_fd);}
            else {s_requesting_fd = -1; ESP_LOGW(TAG, "Pedido de lista WiFi sem FD válido.");}
            if (StorageManager::isWifiCacheValid()) {
                ESP_LOGI(TAG, "Cache WiFi válido, respondendo imediatamente.");
                if (s_requesting_fd != -1) {EventBus::post(EventDomain::NETWORK, EventId::NET_LISTOK, &s_requesting_fd, sizeof(int));s_requesting_fd = -1;}
            } else {
                ESP_LOGI(TAG, "Cache WiFi inválido ou expirado, iniciando scan...");
                // StorageManager::invalidateWifiCache();
                wifi_scan_config_t scan_config = {
                    .ssid = nullptr,
                    .bssid = nullptr,
                    .channel = 0,
                    .show_hidden = true,
                    .scan_type = WIFI_SCAN_TYPE_ACTIVE,
                    .scan_time = {.active = {.min = 100, .max = 1500}}
                };
                esp_err_t ret = esp_wifi_scan_start(&scan_config, false);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Falha ao iniciar scan WiFi: %s", esp_err_to_name(ret));
                    if (s_requesting_fd != -1) {EventBus::post(EventDomain::NETWORK, EventId::NET_LISTFAIL, &s_requesting_fd, sizeof(int));s_requesting_fd = -1;}
                }
            }
        }
        if (evt == EventId::NET_TEST) {
            if (data) {
                s_retry_count = 0;
                const testSSID* received_test_data = static_cast<const testSSID*>(data);
                memcpy(&local_test_data, received_test_data, sizeof(testSSID));
                int fdSend = local_test_data.fd;
                memcpy(&s_requesting_fd, &fdSend, sizeof(int));
                ESP_LOGI(TAG, "NET_TEST: Recebido fd=%d, SSID='%s', Pass='%s'", local_test_data.fd, local_test_data.ssid, local_test_data.pass);
                connectSTA(local_test_data.ssid, local_test_data.pass, true);
            } else {
                ESP_LOGE(TAG, "Teste de WiFi sem SSID/PASS ou dados inválidos no evento NET_TEST.");
            }
        }
        if (evt == EventId::NET_SUSPCLIENT) {
            if (data) {
                uint32_t received_aid = *(uint32_t*)data;
                current_blocked_aid = received_aid;
                blockClientAID();
                ESP_LOGI(TAG, "NET_SUPCLIENT recebido. Bloqueando AID: %" PRIu32"", current_blocked_aid);
            } else {
                ESP_LOGE(TAG, "AID inválido no evento NET_SUSPCLIENT.");
            }
        }
    }
    static void onEventStorageBus(void*,esp_event_base_t base,int32_t id,void*)
    {
        if (static_cast<EventId>(id)==EventId::STO_SSIDOK){ESP_LOGI(TAG,"Iniciando STA");connectSTA();}
        if (static_cast<EventId>(id)==EventId::STO_CONFIGSAVED){
            ESP_LOGI(TAG,"Alterou configuração – iniciando timer de 60s para alterar AP.");
            if(ap_change_timer!=nullptr){
                xTimerStop(ap_change_timer,0);
                xTimerChangePeriod(ap_change_timer,pdMS_TO_TICKS(60000),0);
                xTimerStart(ap_change_timer,0);
            }
        }
    }
    static void onWifiEvent(void*, esp_event_base_t base, int32_t id, void* data)
    {
        if(base==WIFI_EVENT)
        {
            switch (id)
            {
                case WIFI_EVENT_AP_START:
                {
                    dns_server_config_t config = DNS_SERVER_CONFIG_SINGLE("*","WIFI_AP_DEF");
                    start_dns_server(&config);
                    ESP_LOGI(TAG, "AP iniciado");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_APCONNECTED);
                    break;
                }
                case WIFI_EVENT_AP_STOP:
                {
                    ESP_LOGW(TAG, "AP parado");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_APDISCONNECTED);
                    break;
                }
                case WIFI_EVENT_STA_START:
                {
                    ESP_LOGI(TAG, "STA iniciou");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_STASTARTED);
                    break;
                }
                case WIFI_EVENT_STA_STOP:
                {
                    ESP_LOGI(TAG, "WiFi STA parado");
                    StorageManager::scanCache->is_sta_connected=false;
                    EventBus::post(EventDomain::NETWORK, EventId::NET_STASTOPPED);
                    break;
                }
                case WIFI_EVENT_STA_CONNECTED:
                {
                    ESP_LOGI(TAG, "STA conectada");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_STACONNECTED);
                    break;
                }
                case WIFI_EVENT_STA_DISCONNECTED:
                {
                    StorageManager::scanCache->is_sta_connected=false;
                        if (s_retry_count < MAX_RETRY) {
                            s_retry_count++;
                            ESP_LOGW(TAG, "Tentando reconectar... (%d/%d)", s_retry_count, MAX_RETRY);
                            esp_wifi_connect();
                        } else {
                            ESP_LOGE(TAG, "Falha após %d tentativas.", s_retry_count);
                            EventBus::post(EventDomain::NETWORK, EventId::NET_STADISCONNECTED);
                            if (s_isTestConnection) {
                                s_isTestConnection = false;
                                ESP_LOGI(TAG, "[TESTE] Rede inválida.");
                                if(s_isAlreadyTryConnection){
                                    s_retry_count = 0;
                                    connectSTA();
                                    EventBus::post(EventDomain::NETWORK,EventId::NET_TESTFAILREVERT,&s_requesting_fd,sizeof(int));
                                    ESP_LOGI(TAG, "[TESTE] NET_TESTFAILREVERT.");
                                }else{
                                    EventBus::post(EventDomain::NETWORK,EventId::NET_TESTFAILTRY,&s_requesting_fd,sizeof(int));
                                    ESP_LOGI(TAG, "[TESTE] NET_TESTFAILTRY.");
                                }
                            }
                        }
                    break;
                }
                case WIFI_EVENT_AP_STACONNECTED:
                {
                    wifi_event_ap_staconnected_t* info = (wifi_event_ap_staconnected_t*)data;
                    uint32_t aid = info->aid;
                    ESP_LOGI(TAG, "Cliente conectado (AID=%" PRIu32 "", aid);
                    if (current_blocked_aid != 0 && current_blocked_aid == info->aid) {
                        ESP_LOGW(TAG, "Tentativa de conexão de cliente bloqueado (AID=%d). Desconectando.",info->aid);
                        esp_wifi_deauth_sta(info->aid);
                        return;
                    }
                    EventBus::post(EventDomain::NETWORK, EventId::NET_APCLICONNECTED,&aid,sizeof(aid));
                    break;
                }
                case WIFI_EVENT_AP_STADISCONNECTED:
                {
                    wifi_event_ap_staconnected_t* info = (wifi_event_ap_staconnected_t*)data;
                    uint32_t aid = info->aid;
                    ESP_LOGI(TAG, "Cliente desconectado (AID=%" PRIu32 ")", aid);
                    EventBus::post(EventDomain::NETWORK, EventId::NET_APCLIDISCONNECTED,&aid,sizeof(aid));
                    break;
                }
                case WIFI_EVENT_SCAN_DONE: 
                {
                    ESP_LOGI(TAG, "Scan WiFi concluído");
                    s_scan_in_progress = false;
                    uint16_t num_networks = 0;
                    esp_wifi_scan_get_ap_num(&num_networks);
                    const uint16_t MAX_NETWORKS_PROCESS = 50;
                    uint16_t networks_to_process = (num_networks > MAX_NETWORKS_PROCESS) ? MAX_NETWORKS_PROCESS : num_networks;
                    char* buffer_ptr = StorageManager::scanCache->networks_html_ptr;
                    size_t buffer_capacity = MAX_HTML_OPTIONS_BUFFER_SIZE;
                    size_t current_offset = 0;
                    int written_chars = 0;
                    if (buffer_capacity > 0) {buffer_ptr[0] = '\0';}
                    wifi_ap_record_t* ap_records = nullptr;
                    if (networks_to_process == 0) {
                        ESP_LOGW(TAG, "Nenhuma rede encontrada");
                        const char* no_networks_msg = "listNet<option value=''>Nenhuma rede encontrada</option>";
                        written_chars = snprintf(buffer_ptr + current_offset, buffer_capacity - current_offset, "%s", no_networks_msg);
                        if (written_chars > 0 && (size_t)written_chars < buffer_capacity - current_offset) {current_offset += written_chars;}
                        goto cleanup_and_post;
                    }
                    ap_records = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * networks_to_process);
                    if (!ap_records) {
                        ESP_LOGE(TAG, "Falha ao alocar memória para registros de scan.");
                        const char* error_msg = "listNet<option value=''>Erro de memória ao buscar redes</option>";
                        written_chars = snprintf(buffer_ptr + current_offset, buffer_capacity - current_offset, "%s", error_msg);
                        if (written_chars > 0 && (size_t)written_chars < buffer_capacity - current_offset) {current_offset += written_chars;}
                        EventBus::post(EventDomain::NETWORK, EventId::NET_LISTFAIL, &s_requesting_fd, sizeof(int));
                        s_requesting_fd = -1;
                        goto cleanup_and_post;
                    }
                    written_chars = snprintf(buffer_ptr + current_offset, buffer_capacity - current_offset, "listNet");
                    current_offset += written_chars;
                    esp_wifi_scan_get_ap_records(&networks_to_process, ap_records);
                    ESP_LOGI(TAG, "Scan encontrou %d redes processando %d redes", num_networks, networks_to_process);
                    static char current_ssid_char[MAX_SSID_LEN];
                    strncpy(current_ssid_char,StorageManager::cd_cfg->ssid, sizeof(current_ssid_char) - 1);
                    current_ssid_char[sizeof(current_ssid_char) - 1] = '\0';
                    if (!StorageManager::isBlankOrEmpty(current_ssid_char)) {ESP_LOGI(TAG, "SSID atual configurado: %s", current_ssid_char);}
                    for (uint16_t i = 0; i < networks_to_process; i++) {
                        char ssid_temp_char[sizeof(ap_records[i].ssid)];
                        strncpy(ssid_temp_char, (char*)ap_records[i].ssid, sizeof(ssid_temp_char) - 1);
                        ssid_temp_char[sizeof(ssid_temp_char) - 1] = '\0';
                        if (StorageManager::isBlankOrEmpty(ssid_temp_char)) {ESP_LOGD(TAG, "Pulando rede com SSID vazio.");continue;}
                        bool is_current = (strcmp(ssid_temp_char, current_ssid_char) == 0);
                        if(is_current){written_chars=snprintf(buffer_ptr+current_offset,buffer_capacity-current_offset,"<option value='%s' selected>%s</option>",ssid_temp_char,ssid_temp_char);}
                        else{written_chars=snprintf(buffer_ptr+current_offset,buffer_capacity-current_offset,"<option value='%s'>%s</option>",ssid_temp_char,ssid_temp_char);}
                        if(written_chars>0 && (size_t)written_chars<buffer_capacity-current_offset){current_offset+=written_chars;}
                        else{ESP_LOGW(TAG, "Buffer de cache WiFi cheio ou erro de escrita. Parando de adicionar redes.");break;}
                    }
                    if (current_offset < buffer_capacity) {buffer_ptr[current_offset] = '\0';}
                    else if (buffer_capacity > 0) {buffer_ptr[buffer_capacity - 1] = '\0';}
                    StorageManager::scanCache->networks_html_len = current_offset;
                    StorageManager::scanCache->last_scan = time(nullptr);
                    ESP_LOGI(TAG, "Cache WiFi atualizado com %zu bytes.", StorageManager::scanCache->networks_html_len);
                cleanup_and_post:
                    if(s_requesting_fd!=-1){EventBus::post(EventDomain::NETWORK,EventId::NET_LISTOK,&s_requesting_fd,sizeof(int));s_requesting_fd=-1;}
                    if(ap_records){free(ap_records);}
                    break;
                }
                default:
                    break;
            }
        }
        else if(base==IP_EVENT)
        {
            switch (id)
            {
                case IP_EVENT_AP_STAIPASSIGNED: 
                {
                    ESP_LOGI(TAG, "Cliente conectado ao AP recebeu IP");
                    EventBus::post(EventDomain::NETWORK, EventId::NET_APCLIGOTIP);
                    break;
                }
                case IP_EVENT_STA_GOT_IP: 
                {
                    ip_event_got_ip_t* event = (ip_event_got_ip_t*)data;
                    snprintf(StorageManager::id_cfg->ip, sizeof(StorageManager::id_cfg->ip), IPSTR, IP2STR(&event->ip_info.ip));
                    ESP_LOGI(TAG, "STA obteve IP.");
                    s_retry_count = 0;
                    StorageManager::scanCache->is_sta_connected=true;
                    if (s_isTestConnection) {
                        ESP_LOGI(TAG, "[TESTE] Rede válida. IP obtido com sucesso!");
                        s_isTestConnection = false;
                        CredentialConfigDTO credential_dto;
                        strncpy(credential_dto.ssid,local_test_data.ssid,sizeof(credential_dto.ssid)-1);
                        strncpy(credential_dto.password,local_test_data.pass,sizeof(credential_dto.password)-1);
                        credential_dto.ssid[sizeof(credential_dto.ssid)-1]='\0';
                        credential_dto.password[sizeof(credential_dto.password)-1]='\0';
                        RequestSave requester;
                        requester.requester=s_requesting_fd;
                        requester.resquest_type=RequestTypes::REQUEST_NONE;
                        esp_err_t ret=StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::CREDENTIAL_DATA,&credential_dto,sizeof(CredentialConfigDTO),requester,EventId::STO_CREDENTIALSAVED);
                        if (ret != ESP_OK) {ESP_LOGE(TAG, "Falha ao enfileirar requisição de CONFIG_CREDENTIAL");}
                        else {ESP_LOGI(TAG, "Requisição CONFIG_CREDENTIAL enfileirada com sucesso");}
                    }
                    EventBus::post(EventDomain::NETWORK, EventId::NET_STAGOTIP);
                    break;
                }
                default:
                    break;
            }
        }
    }
    esp_err_t startAP()
    {
        // --- Inicialização do NVS ---
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
            ret == ESP_ERR_NVS_NEW_VERSION_FOUND ||
            ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "NVS inválido, apagando...");
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        // --- Inicialização das interfaces e eventos ---
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        // Cria as interfaces AP e STA (as duas, pois o modo será AP+STA)
        netif_ap  = esp_netif_create_default_wifi_ap();
        netif_sta = esp_netif_create_default_wifi_sta();
        // --- Inicializa o Wi-Fi ---
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        // --- Registra handlers de evento ---
        ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr));
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr));
        // --- Configura o Access Point ---
        wifi_config_t ap_cfg{};
        strncpy((char*)ap_cfg.ap.ssid,StorageManager::cfg->central_name,sizeof(ap_cfg.ap.ssid)-1);
        ap_cfg.ap.ssid[sizeof(ap_cfg.ap.ssid)-1]='\0';
        ap_cfg.ap.ssid_len=strlen((char*)ap_cfg.ap.ssid);
        ap_cfg.ap.channel=1;
        ap_cfg.ap.max_connection=4;
        ap_cfg.ap.authmode=WIFI_AUTH_OPEN;
        // --- Define modo e configurações ---
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
        // --- Inicia o Wi-Fi ---
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "Access Point iniciado: SSID=%s, canal=%d", ap_cfg.ap.ssid, ap_cfg.ap.channel);
        return ESP_OK;
    }
    esp_err_t init()
    {
        esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr);
        esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, &onWifiEvent, nullptr);
        EventBus::regHandler(EventDomain::NETWORK, &onEventNetworkBus, nullptr);
        EventBus::regHandler(EventDomain::STORAGE, &onEventStorageBus, nullptr);
        EventBus::regHandler(EventDomain::READY, &onEventReadyBus, nullptr);
        blocked_aid_timer=xTimerCreate("BlockedAIDTimer",pdMS_TO_TICKS(30000),pdFALSE,(void*)0,unblock_aid_timer_callback);
        if(blocked_aid_timer==nullptr){ESP_LOGE(TAG,"Falha ao criar timer para bloqueio de AID!");return ESP_FAIL;}
        ap_change_timer=xTimerCreate("ChangeAPTimer",pdMS_TO_TICKS(45000),pdFALSE,(void*)0,chage_timer_ap);
        if(ap_change_timer==nullptr){ESP_LOGE(TAG,"Falha ao criar AP change timer!");}
        EventBus::post(EventDomain::READY, EventId::NET_READY);
        ESP_LOGI(TAG, "NetManager inicializado (aguardando READY_ALL para iniciar AP)");
        return ESP_OK;
    }
}