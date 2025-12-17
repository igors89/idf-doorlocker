#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string>
#include <vector>
#include "esp_err.h"
#include "esp_log.h"
#include "event_bus.hpp"
#include "storage_manager.hpp"
#include "esp_http_server.h"
#include "esp_netif.h"
#include "cJSON.h"
#include "esp_random.h"
#include "esp_random.h"
#include "freertos/timers.h"
#include <stdexcept>

namespace WebManager {
    // init do web_manager
    esp_err_t init();
    // handlers HTTP
    static esp_err_t root_handler(httpd_req_t* req);
    static esp_err_t serve_static_file_handler(httpd_req_t* req);
    static esp_err_t redirect_to_root_handler(httpd_req_t* req);
    static esp_err_t login_auth_handler(httpd_req_t* req);
    static esp_err_t get_config_handler(httpd_req_t* req);
    static esp_err_t encerrar_handler(httpd_req_t* req);
    static esp_err_t upnp_description_handler(httpd_req_t* req);
    static esp_err_t api_handler(httpd_req_t* req);
    // 
    static void startServer();
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void*);
    static void onWebEvent(void*, esp_event_base_t, int32_t id, void*);
    // static void onSocketEvent(void*, esp_event_base_t, int32_t id, void*);
    static void registerUriHandler(const char* description, httpd_method_t method, esp_err_t (*handler)(httpd_req_t *r));
    static esp_err_t redirect_to_root_handler(httpd_req_t* req);
    static esp_err_t error_404_redirect_handler(httpd_req_t* req, httpd_err_code_t error);
}
