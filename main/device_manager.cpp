
#include "device_manager.hpp"

static const char* TAG = "DeviceManager";

namespace DeviceManager{
    // buzzer
    #define BUZZER_GPIO      GPIO_NUM_15
    #define BUZZER_ACTIVE    0
    #define BUZZER_INACTIVE  1
    #define BUZZER_MS        100
    static esp_timer_handle_t buzzer_timer=nullptr;
    // devices
    // static const gpio_num_t OUTPUT_DEV[4]={GPIO_NUM_41,GPIO_NUM_42,GPIO_NUM_40,GPIO_NUM_39};
    static const touch_pad_t BUTTON_DEV[6]={TOUCH_PAD_NUM4,TOUCH_PAD_NUM5,TOUCH_PAD_NUM6,TOUCH_PAD_NUM7,TOUCH_PAD_NUM8,TOUCH_PAD_NUM9};
    static esp_timer_handle_t timers[6]={nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};
    static uint32_t last_press_time[6]={0,0,0,0,0,0};
    static const uint32_t DEBOUNCE_MS=200;
    static touch_button_handle_t button_handle[6];
    static QueueHandle_t touch_queue=nullptr;
    static QueueHandle_t storage_event_queue=nullptr;
    static void storage_event_task(void* arg);
    // inicializações
    // static void init_gpios(){
    //     for (int i = 0; i < 4; ++i) {
    //         gpio_reset_pin(OUTPUT_DEV[i]);
    //         gpio_set_direction(OUTPUT_DEV[i], GPIO_MODE_OUTPUT);
    //         const Device* device_ptr = StorageManager::getDevice(std::to_string(i));
    //         if(device_ptr){
    //             DeviceDTO device_dto;
    //             memcpy(&device_dto, device_ptr,sizeof(DeviceDTO));
    //             xQueueSend(storage_event_queue,&i,0);
    //         }
    //     }
    //     ESP_LOGI(TAG, "GPIOs de saída inicializadas");
    // }
    static void init_touch(){
        touch_pad_init();
        for (int i = 0; i < 6; ++i){touch_pad_config(BUTTON_DEV[i]);}
        touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER);
        touch_pad_fsm_start();
        touch_elem_global_config_t global_config = TOUCH_ELEM_GLOBAL_DEFAULT_CONFIG();
        ESP_ERROR_CHECK(touch_element_install(&global_config));
        touch_button_global_config_t button_global_config = TOUCH_BUTTON_GLOBAL_DEFAULT_CONFIG();
        ESP_ERROR_CHECK(touch_button_install(&button_global_config));
        for (int i = 0; i < 6; ++i) {
            touch_button_config_t button_config={.channel_num=BUTTON_DEV[i],.channel_sens=0.5F};
            ESP_ERROR_CHECK(touch_button_create(&button_config,&button_handle[i]));
            ESP_ERROR_CHECK(touch_button_subscribe_event(button_handle[i],TOUCH_ELEM_EVENT_ON_PRESS,(void*)i));
            ESP_ERROR_CHECK(touch_button_set_dispatch_method(button_handle[i],TOUCH_ELEM_DISP_CALLBACK));
            ESP_ERROR_CHECK(touch_button_set_callback(button_handle[i],touch_event_cb));
        }
        ESP_ERROR_CHECK(touch_element_start());
        touch_queue = xQueueCreate(4, sizeof(uint8_t));
        xTaskCreatePinnedToCore(touch_task, "touch_task", 4096, NULL, 4, NULL, tskNO_AFFINITY);
        ESP_LOGI(TAG, "Touch Element inicializado");
    }
    static void init_buzzer(){
        gpio_reset_pin(BUZZER_GPIO);
        gpio_set_direction(BUZZER_GPIO,GPIO_MODE_OUTPUT);
        gpio_set_level(BUZZER_GPIO,BUZZER_INACTIVE);
        const esp_timer_create_args_t btz={.callback=&buzzer_timer_cb,.arg=NULL,.dispatch_method=ESP_TIMER_TASK,.name="buzzer_timer"};
        esp_timer_create(&btz,&buzzer_timer);
        ESP_LOGI(TAG, "Buzzer inicializado");
    }
    // buzzer
    static void buzzer_timer_cb(void*){
        gpio_set_level(BUZZER_GPIO,BUZZER_INACTIVE);
    }
    static void buzzer_beep_nonblocking(uint32_t ms){
        if(!buzzer_timer)return;
        gpio_set_level(BUZZER_GPIO,BUZZER_ACTIVE);
        esp_timer_start_once(buzzer_timer,(uint64_t)ms*1000ULL);
    }
    // devices
    static void handlerDev(uint8_t dev_id){
        const Device* device_ptr = StorageManager::getDevice(std::to_string(dev_id));
        if (device_ptr) {
            DeviceDTO device_dto;
            memcpy(&device_dto, device_ptr, sizeof(DeviceDTO));
            if(device_dto.type==1){device_dto.status=1-device_dto.status;}else{device_dto.status=1;}
            RequestSave requester;
            requester.requester=dev_id;
            requester.request_int=dev_id;
            requester.resquest_type=RequestTypes::REQUEST_INT;
            StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::DEVICE_DATA,&device_dto,sizeof(DeviceDTO),requester,EventId::STO_DEVICESAVED);
        }       
    }
    static void touch_event_cb(touch_button_handle_t handle, touch_button_message_t *msg, void *arg){
        if (msg->event != TOUCH_BUTTON_EVT_ON_PRESS) return;
        int dev = (int)arg;
        int64_t now_ms = esp_timer_get_time() / 1000;
        if ((now_ms - last_press_time[dev]) >= (int64_t)DEBOUNCE_MS) {
            last_press_time[dev] = now_ms;
            uint8_t d = dev;
            xQueueSendFromISR(touch_queue,&d,NULL);
            ESP_LOGI(TAG, "Touch detected - device %d", dev);
        }
    }
    static void touch_task(void*){
        uint8_t dev;
        for(;;){if(xQueueReceive(touch_queue,&dev,portMAX_DELAY)){
            if(dev == 0){buzzer_beep_nonblocking(BUZZER_MS);}
            else{buzzer_beep_nonblocking(BUZZER_MS);handlerDev(dev);}}
        }
    }
    static void timer_callback(void* arg){
        int dev_id = *(int*)arg;
        free(arg);
        const Device* device_ptr = StorageManager::getDevice(std::to_string(dev_id));
        if(device_ptr){
            DeviceDTO device_dto;
            memcpy(&device_dto, device_ptr, sizeof(DeviceDTO));
            device_dto.status=0;
            RequestSave requester;
            requester.requester=dev_id;
            requester.request_int=dev_id;
            requester.resquest_type=RequestTypes::REQUEST_INT;
            StorageManager::enqueueRequest(StorageCommand::SAVE,StorageStructType::DEVICE_DATA,&device_dto,sizeof(DeviceDTO),requester,EventId::STO_DEVICESAVED);
        }
    }
    //eventos
    static void storage_event_task(void* arg){
        int devsen_id;
        for(;;){
            if(xQueueReceive(storage_event_queue,&devsen_id,portMAX_DELAY)==pdTRUE){
                if(devsen_id<4){
                    const Device* device_ptr=StorageManager::getDevice(std::to_string(devsen_id));
                    if(device_ptr){
                        DeviceDTO device_dto;
                        memcpy(&device_dto,device_ptr,sizeof(DeviceDTO));
                        if(device_dto.status==0){                            
                            // gpio_set_level(OUTPUT_DEV[devsen_id],1);
                            if(timers[devsen_id]){
                                esp_timer_stop(timers[devsen_id]);
                                esp_timer_delete(timers[devsen_id]);
                                timers[devsen_id] = nullptr;
                            }
                        }else{
                            // gpio_set_level(OUTPUT_DEV[devsen_id],0);                            
                            uint32_t timeout_ms;
                            if(device_dto.type==2)timeout_ms=100;
                            else if(device_dto.type==3)timeout_ms=device_dto.time*1000;
                            else timeout_ms=0;
                            if (timeout_ms > 0) {
                                int *dev_id_ptr = (int*) malloc(sizeof(int));
                                *dev_id_ptr = devsen_id;
                                esp_timer_create_args_t timer_args={.callback=timer_callback,.arg=dev_id_ptr,.dispatch_method=ESP_TIMER_TASK,.name="device_timer",.skip_unhandled_events=true};
                                esp_timer_create(&timer_args, &timers[devsen_id]);
                                esp_timer_start_once(timers[devsen_id], timeout_ms * 1000);
                            }
                        }
                    }
                }
            }
        }
    }
    static void onStorageEvent(void*,esp_event_base_t,int32_t id,void* event_data){
        if(!event_data)return;
        EventId ev=static_cast<EventId>(id);
        if(ev==EventId::STO_DEVICESAVED||ev==EventId::STO_SENSORSAVED){
            EventBus::post(EventDomain::NETWORK, EventId::NET_RTCDEVREQUEST);
            RequestSave requester;
            memcpy(&requester,event_data,sizeof(RequestSave));
            if(requester.resquest_type!=RequestTypes::REQUEST_INT)return;
            int32_t rid=requester.request_int;
            if(storage_event_queue){xQueueSend(storage_event_queue,&rid,0);ESP_LOGI(TAG,"Enfileirado id=% " PRId32 " (storage event % " PRId32 ")",rid,id);}
        }
    }
    static void onReadyEvent(void*,esp_event_base_t,int32_t id,void*) {
        if (static_cast<EventId>(id)==EventId::READY_ALL) {
            EventBus::unregHandler(EventDomain::READY, &onReadyEvent);
            init_touch();
            // init_gpios();
            init_buzzer();
            EventBus::post(EventDomain::DEVICE, EventId::DEV_STARTED);
            ESP_LOGI(TAG, "→ DEV_STARTED publicado");
        }
    }
    static void onNetworkEvent(void*, esp_event_base_t, int32_t id, void* event_data) {
        if (static_cast<EventId>(id) == EventId::NET_STAGOTIP) {
            ESP_LOGI(TAG, "Event NET_STAGOTIP recebido");
            return;
        }
    }
    esp_err_t init(){
        EventBus::regHandler(EventDomain::READY, &onReadyEvent, nullptr);
        EventBus::regHandler(EventDomain::STORAGE, &onStorageEvent, nullptr);
        EventBus::regHandler(EventDomain::NETWORK, &onNetworkEvent, nullptr);
        EventBus::post(EventDomain::READY, EventId::DEV_READY);
        ESP_LOGI(TAG, "Device enviou DEV_READY.");
        return ESP_OK;
    }
}