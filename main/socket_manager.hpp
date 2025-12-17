#pragma once
#include "esp_err.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "event_bus.hpp"
#include <vector>
#include <mutex>
#include <cstring>
#include <algorithm>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_system.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include "esp_netif.h"
#include "esp_eth.h"
#include <string>
#include "cJSON.h"
#include <unistd.h>
#include <sys/socket.h>
#include "esp_timer.h"
#include <set>

#include "storage_manager.hpp"

namespace SocketManager {
    // inits
    esp_err_t init();
    esp_err_t start(httpd_handle_t server);
    esp_err_t stop();
    // Envia mensagem para um cliente específico
    esp_err_t sendToClient(int fd, const char* message);
    static void kill_client_task(void* arg);
    static void removeClientByFd(int fd);
    void handle_ws_alive(int fd);
    static void set_client_from_ap(int fd,bool from_ap);
    static void suspend_ap_for_seconds(uint32_t seconds);
    static void ap_suspend_resume_cb(void* arg);
    // Struct para fd e AID
    struct WebSocketClient {
        int fd; uint32_t aid; bool from_ap; 
        WebSocketClient(int file_descriptor, uint32_t association_id = 0) : fd(file_descriptor), aid(association_id), from_ap(false) {}
        bool operator==(int other_fd) const { return fd == other_fd; }
    };
    struct KillTaskArg{int fd;};
    // Lista de file descriptors dos clientes WebSocket conectados
    static std::vector<WebSocketClient> ws_clients;
    static std::mutex clients_mutex;
    static httpd_handle_t ws_server = nullptr;
    static bool ws_registered = false;
    static httpd_uri_t ws_uri;
    // controle de FDs abertos
    #define SUSPEND_AP 20
    static esp_timer_handle_t ap_suspend_timer = nullptr;
    static wifi_mode_t saved_wifi_mode = WIFI_MODE_NULL;
    static std::mutex ap_suspend_mutex;
    static const int64_t KEEP_INTERVAL_MS = 15*1000;
    static const TickType_t KILL_TASK_DELAY_TICKS = pdMS_TO_TICKS(40);
    static esp_timer_handle_t keep_timer = nullptr;
    static std::set<int> pending_keep_fds;
    static std::mutex pending_mutex;
}