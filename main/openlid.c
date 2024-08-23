#include "openlid.h"

#define LID_GPIO 21

#define ESP_INTR_FLAG_DEFAULT 0
#define DEBOUNCE_LIMIT 50

static QueueHandle_t gpio_evt_queue = NULL;
static bool lid_monitor_active;
static bool lid_open;


static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t lvl_change = 1;

    //sending to Queue a trigger
    xQueueSendFromISR(gpio_evt_queue, &lvl_change, NULL);
}




static void gpio_task(void* arg)
{
    uint32_t lvl_change=0;
    uint32_t gpio_lvl;
    uint32_t last_time = 0;
    for (;;) {
        
        if (xQueueReceive(gpio_evt_queue, &lvl_change, portMAX_DELAY)) { //wait to receive the trigger
            if((esp_log_timestamp()-last_time > DEBOUNCE_LIMIT) && (lid_monitor_active)){ //was the last change longer than debounce and is lid_monitor active?
                last_time = esp_log_timestamp(); //record the last time

                gpio_lvl = gpio_get_level(LID_GPIO); //read GPIO value

                ESP_LOGI("TAG", "lid:%d, gpio:%d", (int)lid_open, (int)gpio_lvl);

                if((gpio_lvl==1)&&(!lid_open)){ //lid closed, received open signal
                    ESP_LOGI("TAG", "the lid opened");
                    fan_pause();
                    mqtt_publish("flame_thrower/lid_open", "1", 0);
                    lid_open=true;
                }

                if((gpio_lvl==0)&&(lid_open))//lid open, received closing signal
                {
                    ESP_LOGI("TAG", "the lid closed");
                    fan_resume();
                    mqtt_publish("flame_thrower/lid_open", "0", 0);
                    lid_open=false;
                }
            }
        }
    }
}


void lid_init(bool b_lid_monitor_active){

    ESP_LOGI("LID_OPEN", "start initializing");

    //zero-initialize the config structure.
    gpio_config_t io_conf = {};
    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    //bit mask of the pins, use GPIO4/5 here
    io_conf.pin_bit_mask = (1ULL<<LID_GPIO);
    //set as input mode
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1; //by default the pin state is 1
    //io_conf.pull_down_en = 1;
    gpio_config(&io_conf);

    //setting active
    lid_monitor_active=b_lid_monitor_active;
    mqtt_publish("flame_thrower/lid","0",0);
    
    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(5, sizeof(uint32_t));
    //start gpio task
    xTaskCreate(gpio_task, "gpio_task", 4096, NULL, 10, NULL);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(LID_GPIO, gpio_isr_handler, (void*) LID_GPIO);

    lid_open = gpio_get_level(LID_GPIO); //lid closed is 0, lid open is 1

    ESP_LOGI("LID_OPEN", "done initializing. Current level is: %d", (int) lid_open);
}


void lid_setActive(bool paramActive){
    lid_monitor_active = paramActive;
    if(lid_monitor_active){mqtt_publish("flame_thrower/lid","1",0);}
    else{mqtt_publish("flame_thrower/lid","0",0);}
}
bool lid_getActive(){
    return lid_monitor_active;
}

bool lid_get_status(){
    return lid_open;
}