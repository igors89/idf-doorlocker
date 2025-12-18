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
        // loadAllDevices();
        // loadAllSensors();
        ESP_LOGI(TAG, "Arquivos carregados na PSRAM.");
        return ESP_OK;
    }
}