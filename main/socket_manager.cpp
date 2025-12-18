#include "socket_manager.hpp"

static const char* TAG = "SocketManager";

namespace SocketManager {
    //Funções info
    static void info(int fd){
        cJSON* root = cJSON_CreateObject();
        if(!root){ESP_LOGE(TAG, "Falha ao criar objeto cJSON");return;}
        cJSON_AddStringToObject(root, "cNome", StorageManager::cfg->central_name);
        cJSON_AddStringToObject(root, "isIA", "TRUEIA");
        cJSON_AddStringToObject(root, "userToken", StorageManager::cfg->token_id);
        cJSON_AddStringToObject(root, "passToken", StorageManager::cfg->token_password);
        cJSON_AddStringToObject(root, "useUserToken", StorageManager::cfg->token_flag);
        cJSON_AddStringToObject(root, "token", StorageManager::id_cfg->id);
        if(StorageManager::scanCache->is_sta_connected){
            cJSON_AddStringToObject(root, "conexao", "con");
        } else {
            cJSON_AddStringToObject(root, "conexao", "dis");
        }
        const char* ssid_value = (strlen(StorageManager::cd_cfg->ssid) == 0) ? "" : StorageManager::cd_cfg->ssid;
        cJSON_AddStringToObject(root, "ssid", ssid_value);
        const char* json_string = cJSON_PrintUnformatted(root);
        if(!json_string){ESP_LOGE(TAG, "Falha ao serializar objeto cJSON");cJSON_Delete(root);return;}
        std::string full_message = "listInfo" + std::string(json_string);
        esp_err_t ret = sendToClient(fd,full_message.c_str());
        if(ret != ESP_OK){ESP_LOGE(TAG,"Falha ao servir listInfo");}
        else{ESP_LOGI(TAG, "Servido listInfo");}
        cJSON_Delete(root);
        free((void*)json_string);
    }
    // inclusão e remoção de FDs, tratamento de AP
    static int log_ap_associated_clients() {
        wifi_sta_list_t sta_list;
        memset(&sta_list, 0, sizeof(sta_list));
        esp_err_t r = esp_wifi_ap_get_sta_list(&sta_list);
        if (r != ESP_OK) {ESP_LOGW(TAG, "esp_wifi_ap_get_sta_list falhou: %s", esp_err_to_name(r));return -1;}
        ESP_LOGI(TAG, "AP associados: %u", sta_list.num);
        for(unsigned i=0;i<sta_list.num;++i){const wifi_sta_info_t &info=sta_list.sta[i];ESP_LOGI(TAG,"idx=%u rssi=%d",i,info.rssi);}
        return (int)sta_list.num;
    }
    static void addClientIfNeeded(int fd, uint32_t aid = 0) {
        std::lock_guard<std::mutex> lk(clients_mutex);
        auto it = std::find_if(ws_clients.begin(), ws_clients.end(), [fd](const WebSocketClient &c){ return c.fd == fd; });
        if(it==ws_clients.end()){ws_clients.emplace_back(fd,aid);ESP_LOGI(TAG,"addClientIfNeeded: fd=%d adicionado em ws_clients",fd);}
        else{ESP_LOGI(TAG,"addClientIfNeeded: fd=%d já presente",fd);}
    }
    static void kill_client_task(void* arg){
        KillTaskArg* a=static_cast<KillTaskArg*>(arg);
        if(!a){vTaskDelete(NULL);return;}
        vTaskDelay(KILL_TASK_DELAY_TICKS);
        int fd = a->fd;
        free(a);
        ESP_LOGW(TAG, "kill_client_task: fechando fd=%d", fd);
        removeClientByFd(fd);
        vTaskDelete(NULL);
    }
    static void removeClientByFd(int fd){
        std::lock_guard<std::mutex> lk(clients_mutex);
        auto it = std::find_if(ws_clients.begin(), ws_clients.end(),
            [fd](const WebSocketClient &c){ return c.fd == fd; });
        if (it != ws_clients.end()) {
            bool was_from_ap = it->from_ap;
            ::shutdown(fd,SHUT_RDWR);
            ::close(fd);
            ws_clients.erase(it);
            ESP_LOGI(TAG, "cliente fd=%d removido da lista", fd);
            if(was_from_ap){ESP_LOGI(TAG, "cliente era do AP -> suspendendo AP por 20s");suspend_ap_for_seconds(SUSPEND_AP);}
            return;
        }
        ::shutdown(fd,SHUT_RDWR);
        ::close(fd);
        ESP_LOGI(TAG, "removeClientByFd: fd=%d fechado (não estava na lista)", fd);
    }
    void handle_ws_alive(int fd){
        std::lock_guard<std::mutex> lk(pending_mutex);
        auto it = pending_keep_fds.find(fd);
        if (it != pending_keep_fds.end()) {pending_keep_fds.erase(it);}
    }
    static void keep_timer_cb(void* arg) {
        (void)arg;
        std::vector<int> to_kill;
        {std::lock_guard<std::mutex> lk(pending_mutex);for (int fd : pending_keep_fds) to_kill.push_back(fd);}
        pending_keep_fds.clear();
        for (int fd : to_kill) {
            KillTaskArg* a = (KillTaskArg*) malloc(sizeof(KillTaskArg));
            if(!a){ESP_LOGE(TAG,"falha malloc para kill task fd=%d; fechando direto", fd);removeClientByFd(fd);continue;}
            a->fd = fd;
            BaseType_t rc = xTaskCreate(kill_client_task, "kill_client", 2048/sizeof(StackType_t), a, tskIDLE_PRIORITY+1, NULL);
            if(rc!=pdPASS){ESP_LOGE(TAG,"falha ao criar kill task para fd=%d; fechando direto", fd);free(a);removeClientByFd(fd);}
            else {ESP_LOGI(TAG,"kill task criada para fd=%d", fd);}
        }
        std::vector<int> current_fds;
        {std::lock_guard<std::mutex> lk(clients_mutex);for (const auto &c : ws_clients) {current_fds.push_back(c.fd);}}
        for (int fd : current_fds) {
            bool ok = (sendToClient(fd, "keep") == ESP_OK);
            if(!ok){ESP_LOGW(TAG,"falha ao enviar keep para fd=%d -> fechar imediatamente", fd);removeClientByFd(fd);continue;}
            {std::lock_guard<std::mutex> lk(pending_mutex);pending_keep_fds.insert(fd);}
        }
    }
    void start_keep_timer() {
        if (keep_timer) return;
        const esp_timer_create_args_t targs={.callback=&keep_timer_cb,.arg=NULL,.dispatch_method=ESP_TIMER_TASK,.name="keepalive"};
        if(esp_timer_create(&targs,&keep_timer)!=ESP_OK){ESP_LOGE(TAG,"esp_timer_create falhou");keep_timer=nullptr;return;}
        if (esp_timer_start_periodic(keep_timer, (uint64_t)KEEP_INTERVAL_MS * 1000ULL) != ESP_OK) {
            ESP_LOGE(TAG, "start_keep_timer: esp_timer_start_periodic falhou");
            esp_timer_delete(keep_timer);
            keep_timer = nullptr;
            return;
        }
        ESP_LOGI(TAG, "start_keep_timer: iniciado, intervalo %d ms", (int)KEEP_INTERVAL_MS);
    }
    void stop_keep_timer() {
        if (!keep_timer) return;
        esp_timer_stop(keep_timer);
        esp_timer_delete(keep_timer);
        keep_timer = nullptr;
        ESP_LOGI(TAG, "stop_keep_timer: timer parado");
    }
    static void set_client_from_ap(int fd, bool from_ap){
        std::lock_guard<std::mutex> lk(clients_mutex);
        auto it = std::find_if(ws_clients.begin(), ws_clients.end(),[fd](const WebSocketClient &c){ return c.fd == fd; });
        if (it != ws_clients.end()) {it->from_ap = from_ap;ESP_LOGI(TAG, "fd=%d marcado from_ap=%d", fd, from_ap ? 1 : 0);}
    }
    static void ap_suspend_resume_cb(void* arg){
        (void)arg;
        ESP_LOGI(TAG, "ap_suspend_resume_cb: restaurando modo WiFi para %d", static_cast<int>(saved_wifi_mode));
        if (esp_wifi_set_mode(saved_wifi_mode) != ESP_OK) {ESP_LOGW(TAG, "falha ao restaurar modo WiFi");}
        if (ap_suspend_timer) {esp_timer_delete(ap_suspend_timer);ap_suspend_timer = nullptr;}
    }
    static void suspend_ap_for_seconds(uint32_t seconds){
        std::lock_guard<std::mutex> lk(ap_suspend_mutex);
        wifi_mode_t cur_mode = WIFI_MODE_NULL;
        if(esp_wifi_get_mode(&cur_mode)!=ESP_OK){ESP_LOGW(TAG, "esp_wifi_get_mode falhou");return;}
        if(!(cur_mode==WIFI_MODE_AP||cur_mode==WIFI_MODE_APSTA)){ESP_LOGI(TAG, "AP nao ativo (mode=%d)", static_cast<int>(cur_mode));return;}
        saved_wifi_mode = cur_mode;
        wifi_mode_t new_mode;
        if(cur_mode==WIFI_MODE_APSTA){new_mode=WIFI_MODE_STA;}else{new_mode=WIFI_MODE_NULL;}
        ESP_LOGI(TAG,"suspendendo AP por %" PRIu32 " s (mode %d -> %d)", seconds, static_cast<int>(cur_mode), static_cast<int>(new_mode));
        if(esp_wifi_set_mode(new_mode)!=ESP_OK){ESP_LOGW(TAG,"falha ao set_mode para %d",static_cast<int>(new_mode));return;}
        if (ap_suspend_timer) {esp_timer_stop(ap_suspend_timer);esp_timer_delete(ap_suspend_timer);ap_suspend_timer = nullptr;}
        esp_timer_create_args_t targs={.callback=&ap_suspend_resume_cb,.arg=nullptr,.dispatch_method=ESP_TIMER_TASK,.name="ap_resume"};
        if(esp_timer_create(&targs,&ap_suspend_timer)!=ESP_OK){ESP_LOGW(TAG,"falha ao criar timer de resume");ap_suspend_timer=nullptr;return;}
        uint64_t us = (uint64_t)seconds * 1000000ULL;
        if(esp_timer_start_once(ap_suspend_timer,us)!=ESP_OK){ESP_LOGW(TAG,"falha timer de resume");esp_timer_delete(ap_suspend_timer);ap_suspend_timer=nullptr;return;}
        ESP_LOGI(TAG, "suspend_ap_for_seconds: timer iniciado por %lu s", seconds);
    }
    // Handler do WebSocket
    static esp_err_t ws_handler(httpd_req_t* req) {
        if (req->method == HTTP_GET) {
            int fd = httpd_req_to_sockfd(req);
            addClientIfNeeded(fd);
            size_t client_count = 0;
            std::lock_guard<std::mutex> lk(clients_mutex);
            client_count = ws_clients.size();
            int ap_count = log_ap_associated_clients();
            ESP_LOGI(TAG, "Handshake WebSocket recebido (fd=%d) — ws_clients=%zu, ap_assoc=%d",fd, client_count, ap_count);
            return ESP_OK;
        }
        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;
        esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
        if (ret != ESP_OK) {
            int fd = httpd_req_to_sockfd(req);
            ESP_LOGE(TAG, "Erro de tamanho do frame para fd=%d: %s. Removendo cliente.", fd, esp_err_to_name(ret));
            removeClientByFd(fd);
            return ret;
        }
        if(ws_pkt.len==0){return ESP_OK;}
        uint8_t* buf = (uint8_t*)calloc(1, ws_pkt.len + 1);
        if(!buf){ESP_LOGE(TAG,"Falha ao alocar memória para payload");return ESP_ERR_NO_MEM;}
        ws_pkt.payload = buf;
        ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
        if (ret != ESP_OK) {
            int fd = httpd_req_to_sockfd(req);
            ESP_LOGE(TAG, "Erro ao receber payload para fd=%d: %s. Removendo cliente.", fd, esp_err_to_name(ret));
            free(buf);
            removeClientByFd(fd);
            return ret;
        }
        buf[ws_pkt.len] = '\0';
        std::string message((char*)buf);
        int fd = httpd_req_to_sockfd(req);
        if (strncmp(message.c_str(),"HOST:",5) == 0) {
            const char* ipH=message.c_str()+5;
            if(!ipH){ESP_LOGI(TAG,"Sem ipH");}
            else{
                if(strcmp(ipH,StorageManager::id_cfg->ip)==0){ESP_LOGI(TAG,"ip: %s",message.c_str());}
                else{set_client_from_ap(fd,true);sendToClient(fd,"navegAP");}
            }
        }else if (message == "NET") {
            ESP_LOGI(TAG, "Comando NET recebido, solicitando lista WiFi para fd=%d", fd);
            EventBus::post(EventDomain::NETWORK, EventId::NET_LISTQRY, &fd, sizeof(int));
            std::string response_msg = "fd" + std::to_string(fd);
            sendToClient(fd,response_msg.c_str());
        }
        else if (message == "INFO") {
            ESP_LOGI(TAG, "Comando INFO recebido para fd=%d", fd);
            info(fd);
        }
        else if (strncmp(message.c_str(),"CONFIG",6) == 0) {
            const char* jsonString = message.c_str() + 6;
            ESP_LOGI(TAG, "Comando CONFIG recebido para fd=%d", fd);
            cJSON* root = cJSON_Parse(jsonString);
            if (root == nullptr) {ESP_LOGE(TAG, "Falha ao fazer parse do JSON CONFIG"); ret = ESP_FAIL;}
            else {
                GlobalConfigDTO config_dto;
                cJSON* cn = cJSON_GetObjectItem(root, "cNome");
                cJSON* tf = cJSON_GetObjectItem(root, "userChoice");
                cJSON* ti = cJSON_GetObjectItem(root, "userTk");
                cJSON* tp = cJSON_GetObjectItem(root, "inpPassTk");
                if (cn) strncpy(config_dto.central_name,cn->valuestring,sizeof(config_dto.central_name)-1);
                if (tf) strncpy(config_dto.token_flag,tf->valuestring,sizeof(config_dto.token_flag)-1);
                if (ti) strncpy(config_dto.token_id,ti->valuestring,sizeof(config_dto.token_id)-1);
                if (tp) strncpy(config_dto.token_password,tp->valuestring,sizeof(config_dto.token_password)-1);
                config_dto.central_name[sizeof(config_dto.central_name)-1]='\0';
                config_dto.token_id[sizeof(config_dto.token_id)-1]='\0';
                config_dto.token_password[sizeof(config_dto.token_password)-1]='\0';
                config_dto.token_flag[sizeof(config_dto.token_flag)-1]='\0';
                RequestSave requester;requester.requester=fd;requester.resquest_type=RequestTypes::REQUEST_NONE;
                esp_err_t ret = StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::CONFIG_DATA,&config_dto,sizeof(GlobalConfigDTO),requester,EventId::STO_CONFIGSAVED);
                if (ret != ESP_OK) {ESP_LOGE(TAG, "Falha ao enfileirar requisição de CONFIG_DATA");}
                else {ESP_LOGI(TAG, "Requisição CONFIG_DATA enfileirada com sucesso");}
                cJSON_Delete(root);
            }
        }
        else if (strncmp(message.c_str(),"CREDENTIAL",10) == 0) {
            const char* jsonString = message.c_str()+10;
            ESP_LOGI(TAG, "Comando CREDENTIAL recebido para fd=%d", fd);
            cJSON* root = cJSON_Parse(jsonString);
            if (root == nullptr) {ESP_LOGE(TAG, "Falha ao fazer parse do JSON CREDENTIAL. JSON recebido: %s", jsonString); ret = ESP_FAIL;}
            else {
                cJSON* ssid = cJSON_GetObjectItem(root,"nomewifi");
                cJSON* pass = cJSON_GetObjectItem(root,"inpSenha");
                testSSID test;
                test.fd = fd;
                strncpy(test.ssid,ssid->valuestring,sizeof(test.ssid));
                strncpy(test.pass,pass->valuestring,sizeof(test.pass));
                EventBus::post(EventDomain::NETWORK,EventId::NET_TEST,&test,sizeof(test));
                cJSON_Delete(root);
            }
        }
        else if (strncmp(message.c_str(),"alive",5) == 0) {
            handle_ws_alive(fd);
        }
        else {ESP_LOGW(TAG, "Comando desconhecido: %s", message.c_str());}
        free(buf);
        return ret;
    }
    // Envia mensagem para um cliente específico
    esp_err_t sendToClient(int fd, const char* message) {
        if (!ws_server || !message) {return ESP_ERR_INVALID_ARG;}
        httpd_ws_frame_t ws_pkt;
        memset(&ws_pkt, 0, sizeof(httpd_ws_frame_t));
        ws_pkt.type = HTTPD_WS_TYPE_TEXT;
        ws_pkt.payload = (uint8_t*)message;
        ws_pkt.len = strlen(message);
        esp_err_t ret = httpd_ws_send_frame_async(ws_server, fd, &ws_pkt);
        if (ret != ESP_OK) {ESP_LOGW(TAG, "Falha ao enviar para fd=%d: %s", fd, esp_err_to_name(ret));}
        return ret;
    }
    //Handler de eventos do EventDomain::NETWORK
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void* data) {
        EventId evt = static_cast<EventId>(id);
        int data_rec = -1;
        if(data){memcpy(&data_rec,data,sizeof(data));ESP_LOGD(TAG,"onNetworkEvent: fd=%d, evt=%d",data_rec,(int)evt);}
        if (evt == EventId::NET_LISTOK) {
            ESP_LOGI(TAG, "NET_LISTOK recebido para fd=%d", data_rec);
            const char* html_content = StorageManager::scanCache->networks_html_ptr;
            size_t content_len = StorageManager::scanCache->networks_html_len;
            if (content_len > 0) {
                esp_err_t ret = sendToClient(data_rec, html_content);
                if (ret == ESP_OK) {ESP_LOGI(TAG, "Lista WiFi enviada para fd=%d (%zu bytes)", data_rec, content_len);}
                else {ESP_LOGE(TAG, "Falha ao enviar lista para fd=%d: %s", data_rec, esp_err_to_name(ret));}
            } else {
                ESP_LOGW(TAG, "Cache WiFi vazio para fd=%d", data_rec);
                const char* error_msg = "listNet<option value=''>Erro: cache vazio</option>";
                sendToClient(data_rec, error_msg);
            }
        }
        else if (evt == EventId::NET_TESTFAILREVERT) {
            ESP_LOGI(TAG, "NET_TESTFAILREVERT recebido para fd=%d", data_rec);
            const char* message = "errorRevert";
            sendToClient(data_rec,message);
        }
        else if (evt == EventId::NET_TESTFAILTRY) {
            ESP_LOGI(TAG, "NET_LISTOK recebido para fd=%d", data_rec);
            const char* message = "errorTry";
            sendToClient(data_rec,message);
        }
        else {ESP_LOGD(TAG, "Evento de rede ignorado: %d", (int)evt);}
    }
    // Inicia o WebSocket (chamado após o HTTP server estar rodando)
    esp_err_t start(httpd_handle_t server) {
        if (!server) {ESP_LOGE(TAG, "HTTP server inválido");return ESP_FAIL;}
        if (ws_registered) {ESP_LOGW(TAG, "WebSocket já registrado, ignorando...");return ESP_OK;}
        ws_server = server;
        ESP_LOGI(TAG, "Registrando WebSocket em /ws...");
        ws_uri.uri = "/ws";
        ws_uri.method = HTTP_GET;
        ws_uri.handler = ws_handler;
        ws_uri.user_ctx = nullptr;
        ws_uri.is_websocket = true;
        ws_uri.handle_ws_control_frames = false;
        ws_uri.supported_subprotocol = nullptr;
        esp_err_t ret = httpd_register_uri_handler(server, &ws_uri);
        if (ret == ESP_OK) {ws_registered = true;start_keep_timer();ESP_LOGI(TAG, "✓ WebSocket registrado com sucesso");}
        else {ESP_LOGE(TAG, "Falha ao registrar WebSocket handler");}
        return ret;
    }
    // Parar o WebSocket
    esp_err_t stop() {
        stop_keep_timer();
        std::lock_guard<std::mutex> lock(clients_mutex);
        ws_clients.clear();
        ws_server = nullptr;
        ws_registered = false;
        ESP_LOGI(TAG, "WebSocket parado");
        return ESP_OK;
    }
    static void onWebEvent(void*, esp_event_base_t, int32_t id, void* data) {
        EventId evt = static_cast<EventId>(id);
        if (evt == EventId::WEB_STARTED) {
            ESP_LOGI(TAG, "WEB_STARTED recebido, iniciando WebSocket...");
            httpd_handle_t* server_ptr = (httpd_handle_t*)data;
            if (server_ptr && *server_ptr) {start(*server_ptr);}
            else {ESP_LOGE(TAG, "HTTP server inválido no evento WEB_STARTED");}
        }
    }
    static void onStorageEvent(void*, esp_event_base_t, int32_t id, void* data) {
        int client_fd = -1;
        EventId evt = static_cast<EventId>(id);
        if(data){memcpy(&client_fd,data,sizeof(int));}
        esp_err_t ret = ESP_OK;
        const char* response;
        if (evt == EventId::STO_CONFIGSAVED) {response = "configOk";}
        else if (evt == EventId::STO_CREDENTIALSAVED) {response = "credentialOk";}
        else {return;}
        ret = sendToClient(client_fd,response);
        if (ret != ESP_OK) {ESP_LOGI(TAG,"onStorageEvent RETURN=%s",esp_err_to_name(ret));}
    }
    // Inicialização
    esp_err_t init() {
        ESP_LOGI(TAG, "Inicializando Socket Manager...");
        EventBus::regHandler(EventDomain::WEB, &onWebEvent, nullptr);
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        EventBus::regHandler(EventDomain::STORAGE, &onStorageEvent, nullptr);
        EventBus::post(EventDomain::READY, EventId::SOC_READY);
        ESP_LOGI(TAG, "→ SOC_READY publicado");
        return ESP_OK;
    }
}