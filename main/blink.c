#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/ledc.h"
#include "esp_log.h"

#include "blink.h"

#define TAG "BLINK"

#define BLINK_PIN 2

static TimerHandle_t blink_timer=NULL;
static int blink_timer_id;
static bool blink_bState;
static uint32_t blink_interval;

void vTimerCallback( TimerHandle_t pxTimer ){
    blink_bState=!blink_bState;
    gpio_set_level(BLINK_PIN, blink_bState);
}


void blink_startTimer(uint32_t start_interval){
    blink_timer_id=0;
    blink_interval=start_interval;
    blink_timer = xTimerCreate("blink_timer", 
                               blink_interval/portTICK_PERIOD_MS, //start with 1 blink per second
                               pdTRUE, //auto reload
                               ( void * )blink_timer_id, 
                               (TimerCallbackFunction_t)vTimerCallback);


    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;//disable interrupt
    io_conf.mode = GPIO_MODE_OUTPUT;//set as output mode
    io_conf.pin_bit_mask = 1ULL<<BLINK_PIN; //second pin
    
    gpio_config(&io_conf);

    blink_bState=true;
    gpio_set_level(BLINK_PIN, blink_bState);
    //ESP_LOGI(TAG, "LED state:%d", gpio_get_level(BLINK_PIN));

    xTimerStart(blink_timer, 0);
}

void blink_changeTimerDelay(uint32_t xNewPeriod){

    if(blink_timer==NULL)return;

    blink_interval=xNewPeriod;
    xTimerChangePeriod(blink_timer, blink_interval/portTICK_PERIOD_MS, 0);

}

uint32_t blink_getInterval(){
    return blink_interval;
}