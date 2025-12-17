#pragma once
#include "esp_err.h"
#include "esp_log.h"
#include "event_bus.hpp"
#include <unordered_map>
#include <string>
#include <vector>
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <cstring>
#include <cstdint>
#include "esp_heap_caps.h"
#include <time.h>
#include <cctype>
#include "esp_mac.h"
#include "esp_wifi.h"
#include <sstream>
#include <cstdlib>

#define MAX_MAC_LEN                     18
#define MAX_ID_LEN                      13
#define MAX_IP_LEN                      16
#define MAX_CENTRAL_NAME_LEN            33
#define MAX_TOKEN_ID_LEN                33
#define MAX_TOKEN_PASSWORD_LEN          65
#define MAX_TOKEN_FLAG_LEN               2
#define MAX_HTML_OPTIONS_BUFFER_SIZE  8192
#define MAX_FILE_PATH_LEN               64
#define MAX_MQTT_CLIENT_ID_LEN          23
#define MAX_MQTT_TOPIC_LEN              64
#define MAX_DEVICE_ID_LEN               23

struct WifiScanCache {
    char* networks_html_ptr;
    size_t networks_html_len;
    time_t last_scan;
    bool is_sta_connected;
    WifiScanCache(){
        networks_html_ptr = nullptr;
        networks_html_len = 0;
        last_scan = 0;
        is_sta_connected = false;
    }
};
struct IDConfig {
    char mac[MAX_MAC_LEN];
    char id[MAX_ID_LEN];
    char ip[MAX_IP_LEN];
    IDConfig() {
        memset(mac, 0, sizeof(mac));
        memset(id, 0, sizeof(id));
        memset(ip, 0, sizeof(ip));
    }
};
struct GlobalConfig {
    char central_name[MAX_CENTRAL_NAME_LEN];
    char token_id[MAX_TOKEN_ID_LEN];
    char token_password[MAX_TOKEN_PASSWORD_LEN];
    char token_flag[MAX_TOKEN_FLAG_LEN];
    GlobalConfig() {
        memset(central_name, 0, sizeof(central_name));
        memset(token_id, 0, sizeof(token_id));
        memset(token_password, 0, sizeof(token_password));
        memset(token_flag, 0, sizeof(token_flag));
    }
};
struct GlobalConfigDTO {
    char central_name[MAX_CENTRAL_NAME_LEN];
    char token_id[MAX_TOKEN_ID_LEN];
    char token_password[MAX_TOKEN_PASSWORD_LEN];
    char token_flag[MAX_TOKEN_FLAG_LEN];
    GlobalConfigDTO() {
        memset(central_name, 0, sizeof(central_name));
        memset(token_id, 0, sizeof(token_id));
        memset(token_password, 0, sizeof(token_password));
        memset(token_flag, 0, sizeof(token_flag));
    }
};
struct CredentialConfig {
    char ssid[MAX_SSID_LEN];
    char password[MAX_PASSPHRASE_LEN];
    CredentialConfig() {
        memset(ssid, 0, sizeof(ssid));
        memset(password, 0, sizeof(password));
    }
};
struct CredentialConfigDTO {
    char ssid[MAX_SSID_LEN];
    char password[MAX_PASSPHRASE_LEN];
    CredentialConfigDTO() {
        memset(ssid, 0, sizeof(ssid));
        memset(password, 0, sizeof(password));
    }
};
struct Device {
    char id[MAX_ID_LEN];
    char name[MAX_CENTRAL_NAME_LEN];
    uint8_t type;
    uint16_t time;
    uint8_t status;
    char x_str[MAX_ID_LEN];
    uint8_t x_int;
    Device() {
        memset(id, 0, sizeof(id));
        memset(name, 0, sizeof(name));
        type = 0;
        time = 0;
        status = 0;
        memset(x_str, 0, sizeof(x_str));
        x_int = 0;
    }
};
struct DeviceDTO {
    char id[MAX_ID_LEN];
    char name[MAX_CENTRAL_NAME_LEN];
    uint8_t type;
    uint16_t time;
    uint8_t status;
    char x_str[MAX_ID_LEN];
    uint8_t x_int;
    DeviceDTO() {
        memset(id, 0, sizeof(id));
        memset(name, 0, sizeof(name));
        type = 0;
        time = 0;
        status = 0;
        memset(x_str, 0, sizeof(x_str));
        x_int = 0;
    }
};
struct Sensor {
    char id[MAX_ID_LEN];
    char name[MAX_CENTRAL_NAME_LEN];
    uint8_t type;
    uint16_t time;
    uint8_t x_int;
    char x_str[MAX_ID_LEN];
    Sensor() {
        memset(id, 0, sizeof(id));
        memset(name, 0, sizeof(name));
        type = 0;
        time = 0;
        x_int = 0;
        memset(x_str, 0, sizeof(x_str));
    }
};
struct SensorDTO {
    char id[MAX_ID_LEN];
    char name[MAX_CENTRAL_NAME_LEN];
    uint8_t type;
    uint16_t time;
    uint8_t x_int;
    char x_str[MAX_ID_LEN];
    SensorDTO() {
        memset(id, 0, sizeof(id));
        memset(name, 0, sizeof(name));
        type = 0;
        time = 0;
        x_int = 0;
        memset(x_str, 0, sizeof(x_str));
    }
};
struct Page {
    void* data;
    size_t size;
    std::string mime;
    Page(char* d, size_t s, const std::string& m) : data(d), size(s), mime(m) {}
};
struct testSSID {
    int fd;
    char ssid[MAX_SSID_LEN];
    char pass[MAX_PASSPHRASE_LEN];
    testSSID() {
        fd = 0;
        memset(ssid, 0, sizeof(ssid));
        memset(pass, 0, sizeof(pass));
    }
};
struct CurrentTime {
    int dayOfWeek; // 0 = Domingo
    int hour;
    int minute;
    CurrentTime():dayOfWeek(0),hour(0),minute(0) {}
};
struct PublishBrokerData {
    char device_id[MAX_MQTT_CLIENT_ID_LEN];
    char payload[MAX_MQTT_TOPIC_LEN];
    PublishBrokerData() {
        memset(device_id, 0, sizeof(device_id));
        memset(payload, 0, sizeof(payload));
    }
    PublishBrokerData(const char* id_cstr, const char* p_cstr) {
        memset(device_id, 0, sizeof(device_id));
        memset(payload, 0, sizeof(payload));
        strncpy(device_id, id_cstr, sizeof(device_id) - 1);
        strncpy(payload, p_cstr, sizeof(payload) - 1);
        device_id[sizeof(device_id) - 1] = '\0';
        payload[sizeof(payload) - 1] = '\0';
    }
};
enum class StorageCommand {SAVE,DELETE};
enum class StorageStructType {CONFIG_DATA,CREDENTIAL_DATA,SENSOR_DATA,DEVICE_DATA,AUTOMA_DATA,SCHEDULE_DATA};
enum class RequestTypes {REQUEST_NONE,REQUEST_INT,REQUEST_CHAR};
struct RequestSave {
    int requester;
    int request_int;
    char request_char[MAX_ID_LEN];
    RequestTypes resquest_type;
    RequestSave() {
        requester = 0;
        request_int = 0;
        memset(request_char, 0, sizeof(request_char));
        resquest_type = RequestTypes::REQUEST_NONE;
    }
};
struct StorageRequest {
    StorageCommand command;
    StorageStructType type;
    void* data_ptr;
    size_t data_len;
    RequestSave requester;
    EventId response_event_id;
    StorageRequest() {
        command = StorageCommand::SAVE;
        type = StorageStructType::CONFIG_DATA;
        data_ptr = nullptr;
        data_len = 0;
        response_event_id = EventId::NONE;
    }
};
namespace StorageManager {
    // Ponteiros para as configurações globais na PSRAM
    extern GlobalConfig* cfg;
    extern IDConfig* id_cfg;
    extern CredentialConfig* cd_cfg;
    extern WifiScanCache* scanCache;
    // Funções de utilidade
    bool isBlankOrEmpty(const char* str);
    bool isWifiCacheValid();
    void invalidateWifiCache();
    std::string replacePlaceholders(const std::string& content, const std::string& search, const std::string& replace);
    std::vector<std::string> splitString(const std::string& s, char delimiter);
    // Funções para gerenciamento de páginas web
    void registerPage(const char* uri, Page* page);
    const Page* getPage(const char* uri);
    // Funções para gerenciamento de dispositivos
    void registerDevice(Device* device);
    const Device* getDevice(const std::string& id);
    size_t getDeviceCount();
    std::vector<std::string> getDeviceIds();
    // Funções para gerenciamento de sensores
    void registerSensor(Sensor* sensor);
    const Sensor* getSensor(const std::string& id);
    size_t getSensorCount();
    std::vector<std::string> getSensorIds();
    // Handlers de eventos
    void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*);
    // Função para enfileirar requisições de armazenamento
    esp_err_t enqueueRequest(StorageCommand cmd,StorageStructType type,const void* data_to_copy,size_t data_len,RequestSave requester,EventId response_event_id=EventId::NONE);
    // Função de inicialização
    esp_err_t init();
}