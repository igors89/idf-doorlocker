
#include "device_manager.hpp"

static const char* TAG = "DeviceManager";

namespace DeviceManager{
    // buzzer
    #define BUZZER_GPIO      GPIO_NUM_15
    #define BUZZER_ACTIVE    0
    #define BUZZER_INACTIVE  1
    #define BUZZER_MS        100

    // RF 433MHz (Baseado no seu pRF=10 do Arduino)
    #define RF_GPIO          GPIO_NUM_2
    #define RF_PULSE_LEN     350  // Tempo base do RC-Switch (Protocolo 1)
    #define RF_REPEAT        2    // Quantas vezes repetir o sinal (garantia)


    // --- CONFIGURAÇÃO DE ENERGIA ---
    #define ENERGY_ADC_UNIT     ADC_UNIT_1
    #define ENERGY_ADC_CHANNEL  ADC_CHANNEL_1 
    #define ENERGY_THRESHOLD    1000  // Valor RAW do Arduino (0-4095)
    #define ENERGY_CHECK_MS     10000 // 10 segundos

    // Configurações de Beep e Lógica
    #define BEEP_LONG_MS     1000
    #define PASS_LEN         6
    #define INPUT_TIMEOUT_MS 3000
    #define INTERVAL         500
    static esp_timer_handle_t buzzer_timer=nullptr;
    static esp_timer_handle_t energy_timer=nullptr;
    // Variáveis ADC
    static adc_oneshot_unit_handle_t adc_handle = nullptr;
    static int s_power_state = 0; // 0=Normal, 1=Sem Energia
    // devices
    static const touch_pad_t BUTTON_DEV[6]={TOUCH_PAD_NUM4,TOUCH_PAD_NUM5,TOUCH_PAD_NUM6,TOUCH_PAD_NUM7,TOUCH_PAD_NUM8,TOUCH_PAD_NUM1};
    static uint32_t last_press_time[6]={0,0,0,0,0,0};
    static const uint32_t DEBOUNCE_MS=200;
    static touch_button_handle_t button_handle[6];
    static QueueHandle_t touch_queue=nullptr;
    static QueueHandle_t storage_event_queue=nullptr;
    static void storage_event_task(void* arg);
    // Variáveis de controle da senha
    static char password_buffer[PASS_LEN+1]={0};    
    static uint8_t input_count = 0;
    static int64_t last_input_time = 0;
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


    // --- Implementação RF 433MHz (Estilo RC-Switch) ---
    static void init_rf(){
        gpio_reset_pin(RF_GPIO);
        gpio_set_direction(RF_GPIO, GPIO_MODE_OUTPUT);
        gpio_set_level(RF_GPIO, 0);
        ESP_LOGI(TAG, "RF 433MHz inicializado no GPIO %d", RF_GPIO);
    }

    // Função auxiliar para enviar pulso High/Low
    static void transmit_pulse(int high_len, int low_len) {
        gpio_set_level(RF_GPIO, 1);
        esp_rom_delay_us(high_len);
        gpio_set_level(RF_GPIO, 0);
        esp_rom_delay_us(low_len);
    }

    // Portagem da função mySwitch.send(code, 24)
    // Protocolo 1: 
    // '0' = 1 High, 3 Low
    // '1' = 3 High, 1 Low
    // Sync = 1 High, 31 Low
    static void send_rf_code(uint32_t code, int length) {
        for (int r = 0; r < RF_REPEAT; r++) {
            for (int i = length - 1; i >= 0; i--) {
                if (code & (1 << i)) {
                    // Bit 1
                    transmit_pulse(RF_PULSE_LEN * 3, RF_PULSE_LEN * 1);
                } else {
                    // Bit 0
                    transmit_pulse(RF_PULSE_LEN * 1, RF_PULSE_LEN * 3);
                }
            }
            // Sync bit (no final de cada pacote)
            transmit_pulse(RF_PULSE_LEN * 1, RF_PULSE_LEN * 31);
        }
        ESP_LOGI(TAG, "RF enviado: %" PRIu32 " (%d bits)", code, length);
    }

    // static void energy_timer_cb(void* arg) {
    //     if (!adc_handle) return;

    //     int adc_raw = 0;
    //     // Lê o valor bruto do ADC (equivalente ao analogRead)
    //     ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ENERGY_ADC_CHANNEL, &adc_raw));

    //     ESP_LOGI(TAG, "Monitor de Energia: Val=%d | Estado=%d", adc_raw, s_power_state);

    //     // Lógica portada:
    //     // Estado 0: Energia Normal
    //     // Estado 1: Modo Bateria (WiFi Desligado)

    //     // CASO 1: Queda de Energia ( analogRead < 1000 )
    //     if (s_power_state == 0 && adc_raw < ENERGY_THRESHOLD) {
    //         ESP_LOGW(TAG, "FALTA DE ENERGIA DETECTADA! Desligando WiFi...");
            
    //         // Desliga WiFi para economizar bateria
    //         esp_wifi_disconnect();
    //         esp_wifi_stop();
    //         esp_wifi_set_mode(WIFI_MODE_NULL);
            
    //         s_power_state = 1; // Marca que estamos sem energia
            
    //         // Opcional: Indicação visual (LED) ou sonora aqui
    //     }
        
    //     // CASO 2: Retorno de Energia ( analogRead > 1000 )
    //     else if (s_power_state == 1 && adc_raw > ENERGY_THRESHOLD) {
    //         ESP_LOGW(TAG, "RETORNO DE ENERGIA DETECTADO! Reiniciando sistema...");
            
    //         // Soft Reset conforme solicitado
    //         vTaskDelay(pdMS_TO_TICKS(1000)); // Pequeno delay para estabilizar
    //         esp_restart();
    //     }
    // }

    // static void init_energy() {
    //     // 1. Configuração do ADC OneShot
    //     adc_oneshot_unit_init_cfg_t init_config = {
    //         .unit_id = ENERGY_ADC_UNIT,
    //         .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
    //     };
    //     ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    //     // 2. Configuração do Canal (Atenuação para ler até ~3.3V)
    //     adc_oneshot_chan_cfg_t config = {
    //         .atten = ADC_ATTEN_DB_12,           // Permite leitura full-range (até ~3.1V+)
    //         .bitwidth = ADC_BITWIDTH_DEFAULT, // Geralmente 12 bits (0-4095)
    //     };
    //     ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ENERGY_ADC_CHANNEL, &config));

    //     // 3. Configuração do Timer de 10 segundos
    //     const esp_timer_create_args_t energy_timer_args = {
    //         .callback = &energy_timer_cb,
    //         .arg = NULL,
    //         .dispatch_method = ESP_TIMER_TASK,
    //         .name = "energy_timer"
    //     };
    //     ESP_ERROR_CHECK(esp_timer_create(&energy_timer_args, &energy_timer));
        
    //     // Inicia o timer periodicamente (10s)
    //     // ESP_ERROR_CHECK(esp_timer_start_periodic(energy_timer, ENERGY_CHECK_MS * 1000ULL));
        
    //     ESP_LOGI(TAG, "Monitor de Energia inicializado (ADC1_CH1 / GPIO2)");
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
            touch_button_config_t button_config={.channel_num=BUTTON_DEV[i],.channel_sens=0.1F};
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
        if (esp_timer_is_active(buzzer_timer)) {
            esp_timer_stop(buzzer_timer);
        }
        gpio_set_level(BUZZER_GPIO,BUZZER_ACTIVE);
        esp_timer_start_once(buzzer_timer,(uint64_t)ms*1000ULL);
    }
    // Auxiliar para tocar a sequência de sucesso (3 beeps curtos)
    static void play_success_sequence(){
        for(int i=0; i<3; i++){
            buzzer_beep_nonblocking(BUZZER_MS);
            // Delay pequeno para criar o intervalo entre beeps (beep + pausa)
            vTaskDelay(pdMS_TO_TICKS(BUZZER_MS*2));
        }
    }
    static void touch_event_cb(touch_button_handle_t handle, touch_button_message_t *msg, void *arg){
        if (msg->event != TOUCH_BUTTON_EVT_ON_PRESS) return;
        int dev = (int)arg;
        int64_t now_ms = esp_timer_get_time() / 1000;
        if ((now_ms - last_press_time[dev]) >= (int64_t)DEBOUNCE_MS) {
            last_press_time[dev] = now_ms;
            uint8_t d = dev+1;
            xQueueSendFromISR(touch_queue,&d,NULL);
            // ESP_LOGI(TAG, "Touch detected");
        }
    }
    static void touch_task(void*){
        uint8_t dev;
        for(;;){
            if(xQueueReceive(touch_queue,&dev,pdMS_TO_TICKS(100)) == pdTRUE){
                
                buzzer_beep_nonblocking(BUZZER_MS);
                ESP_LOGI(TAG, "Botão Touch pressionado: %d", dev);

                int64_t now = esp_timer_get_time() / 1000;

                // Beep de feedback visual/sonoro do clique
                buzzer_beep_nonblocking(BUZZER_MS); 
                ESP_LOGI(TAG, "Tecla pressionada: %d", dev);

                // Se passou muito tempo desde o último dígito (sem ter estourado o timeout no else), reseta
                if(input_count > 0 && (now - last_input_time > INPUT_TIMEOUT_MS)){
                     ESP_LOGW(TAG, "Timeout entre cliques (reset forçado). Reiniciando buffer.");
                     input_count = 0;
                }

                last_input_time = now;

                // Armazena no buffer
                if(input_count < PASS_LEN){
                    password_buffer[input_count++] = (char)(dev + '0');
                }

                // Verifica se completou a senha
                if(input_count == PASS_LEN){
                    ESP_LOGI(TAG, "Tags cadastradas = %s", StorageManager::cfg->keys);
                    ESP_LOGI(TAG, "Tag inserida = %s", password_buffer);

                    if(!StorageManager::isBlankOrEmpty(StorageManager::cfg->keys) && (strstr(StorageManager::cfg->keys,password_buffer)!=nullptr)) {play_success_sequence(); send_rf_code(123456, 24); }
                    else {buzzer_beep_nonblocking(BEEP_LONG_MS);}

                    vTaskDelay(pdMS_TO_TICKS(INTERVAL));

                    xQueueReset(touch_queue);
                    
                    // TODO: Aqui você validaria a senha no futuro
                    // Por enquanto apenas reseta para a próxima tentativa
                    input_count = 0;
                    memset(password_buffer, 0, sizeof(password_buffer));
                    last_input_time = esp_timer_get_time() / 1000;
                }
            } else {
                // Timeout da Queue (Nenhum botão apertado nos últimos 100ms)
                if(input_count > 0){
                    int64_t now = esp_timer_get_time() / 1000;
                    if((now - last_input_time) > INPUT_TIMEOUT_MS){
                        ESP_LOGW(TAG, "Timeout de 3s excedido! Rejeitando entrada.");
                        
                        // Beep longo de rejeição
                        buzzer_beep_nonblocking(BEEP_LONG_MS);
                        vTaskDelay(pdMS_TO_TICKS(INTERVAL));

                        xQueueReset(touch_queue);
                        
                        // Reseta o processo
                        input_count = 0;
                        memset(password_buffer, 0, sizeof(password_buffer));
                        last_input_time = esp_timer_get_time() / 1000;
                    }
                }
            }
        }
    }
    //eventos
    static void storage_event_task(void* arg){
        int devsen_id;
        for(;;){
            if(xQueueReceive(storage_event_queue,&devsen_id,portMAX_DELAY)==pdTRUE){
            }
        }
    }
    static void onStorageEvent(void*,esp_event_base_t,int32_t id,void* event_data){
        if(!event_data)return;
        EventId ev=static_cast<EventId>(id);
        if(ev==EventId::STO_DEVICESAVED){
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
            init_rf();  
            // init_energy();
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