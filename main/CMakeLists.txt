idf_component_register(
    SRCS 
        "main.cpp"
        "event_bus.cpp"
        "net_manager.cpp"
        "sync_manager.cpp"
        "storage_manager.cpp"
        "storage.cpp"
        "socket_manager.cpp"
        "web_manager.cpp"
        "udp_manager.cpp"
        "mqtt_manager.cpp"
        "matter_manager.cpp"
        "device_manager.cpp"
        "ble_manager.cpp"
        # "RCSwitch.cpp"
        #

    INCLUDE_DIRS 
        "."
        "../components/dns_server/include"
        "../components/RCSwitch/include"
    
    REQUIRES
        esp_common
        nvs_flash
        esp_wifi
        esp_http_server
        esp_eth
        esp_adc
        esp_timer
        cjson
        driver
        espressif__touch_element
        esp_http_client
        app_update
        esp_partition
        mqtt
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++20
    -Wno-return-type
    -Wno-missing-field-initializers
    -Wno-unused-function
)
target_compile_options(${COMPONENT_LIB} PRIVATE -include inttypes.h)