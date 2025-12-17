#include "storage.hpp"

static const char* TAG = "Storage";

namespace Storage {
    static void loadGlobalConfigFile() {
        const char* path = "/littlefs/config/config";
        FILE* f = fopen(path, "r");
        if(!f){ESP_LOGW(TAG, "Arquivo ausente: %s", path);return;}
        char line[128];
        int index = 0;
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            switch (index) {
                case 0:if(*line){strncpy(StorageManager::cfg->central_name,line,sizeof(StorageManager::cfg->central_name)-1);StorageManager::cfg->central_name[sizeof(StorageManager::cfg->central_name)-1]='\0';}break;
                case 1:if(*line){strncpy(StorageManager::cfg->token_id,line,sizeof(StorageManager::cfg->token_id)-1);StorageManager::cfg->token_id[sizeof(StorageManager::cfg->token_id)-1]='\0';}break;
                case 2:if(*line){strncpy(StorageManager::cfg->token_password,line,sizeof(StorageManager::cfg->token_password)-1);StorageManager::cfg->token_password[sizeof(StorageManager::cfg->token_password)-1]='\0';}break;
                case 3:if(*line){strncpy(StorageManager::cfg->token_flag,line,sizeof(StorageManager::cfg->token_flag)-1);StorageManager::cfg->token_flag[sizeof(StorageManager::cfg->token_flag)-1]='\0';}break;
            }
            ++index;
        }
        fclose(f);
        ESP_LOGI(TAG, "Configuração /config/config carregada (%d linhas)", index);
    }
    static void loadCredentialConfigFile() {
        const char* path = "/littlefs/config/credential";
        FILE* f = fopen(path, "r");
        if(!f){ESP_LOGW(TAG, "Arquivo ausente: %s", path);return;}
        char line[128];
        int index = 0;
        while (fgets(line, sizeof(line), f)) {
            line[strcspn(line, "\r\n")] = 0;
            switch (index) {
                case 0:if(*line){strncpy(StorageManager::cd_cfg->ssid,line,sizeof(StorageManager::cd_cfg->ssid)-1);StorageManager::cd_cfg->ssid[sizeof(StorageManager::cd_cfg->ssid)-1]='\0';}break;
                case 1:if(*line){strncpy(StorageManager::cd_cfg->password,line,sizeof(StorageManager::cd_cfg->password)-1);StorageManager::cd_cfg->password[sizeof(StorageManager::cd_cfg->password)-1]='\0';}break;
            }
            ++index;
        }
        fclose(f);
        ESP_LOGI(TAG, "Configuração /config/config carregada (%d linhas)", index);
    }
    static void loadFileToPsram(const char* path, const char* k, const char* m) {
        FILE* f = fopen(path, "rb");
        if (!f) {ESP_LOGW(TAG, "Arquivo ausente: %s", path);return;}
        fseek(f, 0, SEEK_END);
        size_t sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        void* buf = heap_caps_malloc(sz, MALLOC_CAP_SPIRAM);
        if(!buf){ESP_LOGE(TAG, "Falha ao alocar PSRAM (%s)", k);fclose(f);return;}
        fread(buf, 1, sz, f);
        fclose(f);
        void* page_mem = heap_caps_malloc(sizeof(Page), MALLOC_CAP_SPIRAM);
        if (!page_mem) {ESP_LOGE(TAG, "Falha ao alocar PSRAM (%s)", k);heap_caps_free(buf);return;}
        Page* p_psram = reinterpret_cast<Page*>(page_mem);
        memset(p_psram, 0, sizeof(Page));
        p_psram->data = buf;
        p_psram->size = sz;
        p_psram->mime = m;
        StorageManager::registerPage(k, p_psram);
    }
    static void loadDeviceFile(const std::string& stdPath, const std::string& deviceID) {
        const char* path = stdPath.c_str();
        ESP_LOGI(TAG, "disp para registrar: %s", deviceID.c_str());
        const char* id_from_filename = deviceID.c_str();
        FILE* f = fopen(path, "r");
        if(!f){ESP_LOGW(TAG, "Arquivo de dispositivo ausente: %s", path);return;}
        void* dev_mem = heap_caps_malloc(sizeof(Device), MALLOC_CAP_SPIRAM);
        if(!dev_mem){ESP_LOGE(TAG, "Falha ao alocar PSRAM %s", id_from_filename);fclose(f);return;}
        Device* dev = reinterpret_cast<Device*>(dev_mem);
        memset(dev, 0, sizeof(Device));
        strncpy(dev->id, id_from_filename, sizeof(dev->id) - 1);dev->id[sizeof(dev->id) - 1] = '\0';
        char line[128];
        int index = 0;
        while (fgets(line, sizeof(line), f) && index < 6) {
            line[strcspn(line, "\r\n")] = 0;
            switch (index) {
                case 0:strncpy(dev->name, line, sizeof(dev->name) - 1);dev->name[sizeof(dev->name) - 1] = '\0';break;
                case 1:dev->type = static_cast<uint8_t>(atoi(line));break;
                case 2:dev->time = static_cast<uint16_t>(atoi(line));break;
                case 3:dev->status = static_cast<uint8_t>(atoi(line));break;
                case 4:strncpy(dev->x_str, line, sizeof(dev->x_str) - 1);dev->x_str[sizeof(dev->x_str) - 1] = '\0';break;
                case 5:dev->x_int = static_cast<uint8_t>(atoi(line));break;
            }
            ++index;
        }
        fclose(f);
        StorageManager::registerDevice(dev);
    }
    void loadAllDevices() {
        const char* dir_path = "/littlefs/device";
        DIR* dir = opendir(dir_path);
        if(!dir){ESP_LOGW(TAG, "Diretório /device não encontrado");return;}
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {continue;}
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            ESP_LOGI(TAG,"nome disp:%s ",entry->d_name);
            loadDeviceFile(std::string(full_path), std::string(entry->d_name));
        }
        closedir(dir);
        ESP_LOGI(TAG, "Total de dispositivos: %zu", StorageManager::getDeviceCount());
    }
    esp_err_t saveDeviceFile(Device* device) {
        if (!device) {ESP_LOGE(TAG, "Ponteiro de dispositivo nulo para salvar.");return ESP_ERR_INVALID_ARG;}
        std::string full_path = std::string("/littlefs/device/") + std::string(device->id);
        const char* file_path = full_path.c_str();
        FILE* f = fopen(file_path, "w");
        if (!f) {ESP_LOGE(TAG, "Falha ao abrir arquivo para escrita: %s", file_path);return ESP_FAIL;}
        fprintf(f, "%s\n", device->name);
        fprintf(f, "%u\n", device->type);
        fprintf(f, "%u\n", device->time);
        fprintf(f, "%u\n", device->status);
        fprintf(f, "%s\n", device->x_str);
        fprintf(f, "%u\n", device->x_int);
        fclose(f);
        ESP_LOGI(TAG, "Dispositivo '%s' salvo em %s", device->id, file_path);
        return ESP_OK;
    }
    static void loadSensorFile(std::string stdPath, std::string sensorID) {
        const char* path = stdPath.c_str();
        ESP_LOGI(TAG, "sens para registrar: %s", sensorID.c_str());
        const char* id_from_filename = sensorID.c_str();
        FILE* f = fopen(path, "r");
        if(!f){ESP_LOGW(TAG, "Arquivo de sensor ausente: %s", path);return;}
        void* sen_mem = heap_caps_malloc(sizeof(Sensor), MALLOC_CAP_SPIRAM);
        if(!sen_mem){ESP_LOGE(TAG, "Falha ao alocar PSRAM %s", id_from_filename);fclose(f);return;}
        Sensor* sen = reinterpret_cast<Sensor*>(sen_mem);
        memset(sen, 0, sizeof(Sensor));
        strncpy(sen->id,id_from_filename,sizeof(sen->id)-1);sen->id[sizeof(sen->id) - 1] = '\0';
        char line[128];
        int index = 0;
        while (fgets(line, sizeof(line), f) && index < 6) {
            line[strcspn(line, "\r\n")] = 0;
            switch (index) {
                case 0:strncpy(sen->name, line, sizeof(sen->name) - 1);sen->name[sizeof(sen->name) - 1] = '\0';break;
                case 1:sen->type = static_cast<uint8_t>(atoi(line));break;
                case 2:sen->time = static_cast<uint16_t>(atoi(line));break;
                case 3:sen->x_int = static_cast<uint8_t>(atoi(line));break;
                case 4:strncpy(sen->x_str, line, sizeof(sen->x_str) - 1);sen->x_str[sizeof(sen->x_str) - 1] = '\0';break;
            }
            ++index;
        }
        fclose(f);
        StorageManager::registerSensor(sen);
    }
    void loadAllSensors() {
        const char* dir_path = "/littlefs/sensor";
        DIR* dir = opendir(dir_path);
        if(!dir){ESP_LOGW(TAG, "Diretório /sensor não encontrado");return;}
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {continue;}
            char full_path[512];
            snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
            ESP_LOGI(TAG,"nome sens:%s ",entry->d_name);
            loadSensorFile(std::string(full_path), std::string(entry->d_name));
        }
        closedir(dir);
        ESP_LOGI(TAG, "Total de sensores: %zu", StorageManager::getSensorCount());
    }
    esp_err_t saveSensorFile(Sensor* sensor) {
        if (!sensor) {ESP_LOGE(TAG, "Ponteiro de sensor nulo para salvar.");return ESP_ERR_INVALID_ARG;}
        std::string full_path = std::string("/littlefs/sensor/") + std::string(sensor->id);
        const char* file_path = full_path.c_str();
        FILE* f = fopen(file_path, "w");
        if (!f) {ESP_LOGE(TAG, "Falha ao abrir arquivo para escrita: %s", file_path);return ESP_FAIL;}
        // ainda tem que ver como sensor vem...
        fprintf(f, "%s\n", sensor->name);
        fprintf(f, "%u\n", sensor->type);
        fprintf(f, "%u\n", sensor->time);
        fprintf(f, "%u\n", sensor->x_int);
        fprintf(f, "%s\n", sensor->x_str);
        fclose(f);
        ESP_LOGI(TAG,"Sensor '%s' salvo em %s",sensor->id,file_path);
        return ESP_OK;
    }
    esp_err_t saveGlobalConfigFile(GlobalConfig* cfg) {
        if(!cfg){ESP_LOGE(TAG,"Ponteiro GlobalConfig nulo na salva.");return ESP_ERR_INVALID_ARG;}
        const char* path = "/littlefs/config/config";
        FILE* f = fopen(path, "w");
        if(!f){ESP_LOGE(TAG, "Falha ao abrir arquivo LittleFS para escrita: %s", path);return ESP_FAIL;}
        fprintf(f, "%s\n", cfg->central_name);
        fprintf(f, "%s\n", cfg->token_id);
        fprintf(f, "%s\n", cfg->token_password);
        fprintf(f, "%s\n", cfg->token_flag);
        if(ferror(f)){ESP_LOGE(TAG, "Erro de escrita no arquivo LittleFS: %s", path);fclose(f);return ESP_FAIL;}
        fclose(f);
        ESP_LOGI(TAG, "Configuração salva com sucesso em LittleFS: %s", path);
        return ESP_OK;
    }
    esp_err_t saveCredentialConfigFile(CredentialConfig* cd_cfg) {
        if(!cd_cfg){ESP_LOGE(TAG,"Ponteiro CredentialConfig nulo na salva.");return ESP_ERR_INVALID_ARG;}
        const char* path = "/littlefs/config/credential";
        FILE* f = fopen(path, "w");
        if(!f){ESP_LOGE(TAG, "Falha ao abrir arquivo LittleFS para escrita: %s", path);return ESP_FAIL;}
        fprintf(f, "%s\n", cd_cfg->ssid);
        fprintf(f, "%s\n", cd_cfg->password);
        if(ferror(f)){ESP_LOGE(TAG, "Erro de escrita no arquivo LittleFS: %s", path);fclose(f);return ESP_FAIL;}
        fclose(f);
        ESP_LOGI(TAG, "Credenciais salvas com sucesso em LittleFS: %s", path);
        return ESP_OK;
    }
    esp_err_t init() {
        ESP_LOGI(TAG, "Montando LittleFS...");
        esp_vfs_littlefs_conf_t conf = {"/littlefs","littlefs",nullptr,false,false,false,false};
        esp_err_t ret = esp_vfs_littlefs_register(&conf);
        if(ret!=ESP_OK){ESP_LOGE(TAG,"Falha ao montar LittleFS (%s)",esp_err_to_name(ret));return ret;}
        ESP_LOGI(TAG, "LittleFS montado com sucesso, carregando arquivos...");
        loadGlobalConfigFile();
        loadCredentialConfigFile();
        loadFileToPsram("/littlefs/index.html","index.html","text/html");
        loadFileToPsram("/littlefs/agenda.html","agenda.html","text/html");
        loadFileToPsram("/littlefs/automacao.html","automacao.html","text/html");
        loadFileToPsram("/littlefs/atualizar.html","atualizar.html","text/html");
        loadFileToPsram("/littlefs/central.html","central.html","text/html");
        loadFileToPsram("/littlefs/css/igra.css","css/igra.css","text/css");
        loadFileToPsram("/littlefs/css/bootstrap.min.css","css/bootstrap.min.css","text/css");
        loadFileToPsram("/littlefs/js/messages.js","js/messages.js","application/javascript");
        loadFileToPsram("/littlefs/js/icons.js","js/icons.js","application/javascript");
        loadFileToPsram("/littlefs/img/logomarca","img/logomarca","image/png");
        loadFileToPsram("/littlefs/img/favicon.ico","favicon.ico","image/x-icon");
        loadFileToPsram("/littlefs/ha/description.xml","description.xml","text/xml");
        loadFileToPsram("/littlefs/ha/apiget.json","apiget.json","application/json");
        loadFileToPsram("/littlefs/ha/lights_all.json", "lights_all.json", "application/json");
        loadFileToPsram("/littlefs/ha/light_detail.json", "light_detail.json", "application/json");
        loadAllDevices();
        loadAllSensors();
        ESP_LOGI(TAG, "Arquivos carregados na PSRAM.");
        return ESP_OK;
    }
}