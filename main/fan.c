#include "driver/ledc.h"
#include "esp_log.h"
#include "fan.h"
#include "screen.h"

#define LEDC_TIMER              LEDC_TIMER_0
//#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
//#define LEDC_DUTY               (4096) // Set duty to 50%. (2 ** 13) * 50% = 4096
#define LEDC_MODE               LEDC_LOW_SPEED_MODE


FAN fan;

#define TAG "FAN"

static void fan_change_duty(int duty){
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, fan.m_channel, duty));
    // Update duty to apply the new value
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, fan.m_channel));
  ESP_LOGI(TAG, "Changing duty to: %d", duty);
}

static void ledc_init(int channel, int frequency, int resolution, int gpio)
{
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = resolution,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = frequency,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .speed_mode     = LEDC_MODE,
        .channel        = channel,
        .timer_sel      = LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = gpio,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}



bool fan_init(unsigned int gpio, unsigned int aIn1, unsigned int aIn2, unsigned int stand_by, unsigned int start_speed){
    
    
    fan.m_gpio                = gpio;
    fan.m_channel             = 0; //start with channel 0
    fan.m_speed               = start_speed;
    fan.m_resolution          = 8;
    fan.m_aIn1_pin            = aIn1;
    fan.m_aIn2_pin            = aIn2;
    fan.m_standby_pin         = stand_by;
    fan.m_speed_before_pause  = 0;
    fan.m_frequency           = 500;
    

    //setup GPIOs

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;//disable interrupt
    io_conf.mode = GPIO_MODE_OUTPUT;//set as output mode
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    
    if(gpio_config(&io_conf)!=ESP_OK){
      ESP_LOGE(TAG, "GPIO config failed.");
      return false;
    }

    
    fan.m_aIn1_pos    = 1;
    fan.m_aIn2_pos    = 0;
    fan.m_standby_pos = 1;

    gpio_set_level(fan.m_aIn1_pin,    fan.m_aIn1_pos);
    gpio_set_level(fan.m_aIn2_pin,    fan.m_aIn2_pos);
    


    ledc_init(fan.m_channel, fan.m_frequency, fan.m_resolution, fan.m_gpio);
    //setup ledc
    //ledcSetup(m_channel, m_frequency, m_resolution); // Channel 0, 5 kHz PWM frequency, 8-bit resolution
    //ledcAttachPin(m_gpio, m_channel);

    fan.m_initiated=true;

    fan_set_speed(start_speed);

    //launch speed controller
    gpio_set_level(fan.m_standby_pin, fan.m_standby_pos);
   
    ESP_LOGI(TAG, "Init successful");

    return true;
}

long fan_map(long x, long in_min, long in_max, long out_min, long out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

bool fan_set_speed(unsigned int speed){
  if(!fan.m_initiated){
    ESP_LOGE(TAG, "Cannot set fan speed. Not initialized!");
    return false;
  }

  fan.m_speed = speed;
  fan.m_duty = speed;
  scr_set_fan_speed(speed);
  
  if(speed>0){
    fan.m_duty = fan_map(speed, 0, 100, 60, 255);
    
    if(fan.m_duty<=80){
      fan.m_frequency = fan_map(fan.m_duty, 60, 80, 225, 150);
    }
    else if(fan.m_duty<=100){
      fan.m_frequency = fan_map(fan.m_duty, 81, 100, 150, 100);
    }
    else{
      fan.m_frequency = 100;
    }
  }

  ESP_LOGI(TAG, "Setting speed to: %d, duty:%d, freq:%d", speed, fan.m_duty, fan.m_frequency);
  fan_set_duty(fan.m_duty);
  fan_set_frequency(fan.m_frequency);

  return true;
}
  
bool fan_set_frequency(unsigned int frequency){
  if(!fan.m_initiated){
    ESP_LOGE(TAG, "Cannot set fan frequency. Not initialized!");
    return false;
  }
  fan.m_frequency = frequency;

  
  if(ledc_set_freq(LEDC_MODE, LEDC_TIMER, fan.m_frequency)==ESP_OK ){
    ESP_LOGI(TAG, "Changed frequency to:%d", fan.m_frequency);
  }
  else{
    ESP_LOGE(TAG, "Unknown error: unable to change frequency.");
  }
  
  //ledcSetup(fan.m_channel, frequency, fan.m_resolution);
  return true;
}


unsigned int fan_get_frequency(){
  return fan.m_frequency;    
}

unsigned int fan_get_duty(){
  return fan.m_duty;
}

unsigned int fan_get_channel(){
  return fan.m_channel;
}

bool fan_set_duty(unsigned int duty){
  if(!fan.m_initiated){
    ESP_LOGE(TAG, "Cannot set duty. Not initialized!");
    return false;
  }
  fan.m_duty = duty;

  fan_change_duty(fan.m_duty);
  
  return true;
}


bool fan_set_stand_by(bool stand_by){
  if(!fan.m_initiated){
    ESP_LOGE(TAG, "Cannot set stand-by. Not initialized!");
    return false;
  }

  gpio_set_level(fan.m_standby_pin, stand_by);
  ESP_LOGI(TAG, "Stand-by pin changed to: %d", stand_by);

  return true;
}

bool fan_in_pins_reverse(){
  if(!fan.m_initiated){
    ESP_LOGE(TAG, "Cannot reverse pins. Not initialized!");
    return false;
  }

  fan.m_aIn1_pos=!fan.m_aIn1_pos;
  fan.m_aIn2_pos=!fan.m_aIn2_pos;
  
  gpio_set_level(fan.m_aIn1_pin, fan.m_aIn1_pos);
  gpio_set_level(fan.m_aIn2_pin, fan.m_aIn2_pos);

  ESP_LOGI(TAG, "Pins reversed.");

  return true;
}

bool fan_pause(){
  if(!fan.m_initiated){
    ESP_LOGE(TAG, "Cannot pause. Not initialized!");
    return false;
  }

  fan.m_speed_before_pause = fan.m_speed;
  fan.m_speed=0;

  ESP_LOGI(TAG, "Fan paused. Fan speed changed from:%d to:%d", fan.m_speed_before_pause, fan.m_speed);

  return fan_set_speed(fan.m_speed);
}

bool fan_resume(){
  if(!fan.m_initiated){
    ESP_LOGE(TAG, "Cannot resume. Not initialized!");
    return false;
  }
  fan.m_speed = fan.m_speed_before_pause;
  fan.m_speed_before_pause=0;
  
  ESP_LOGI(TAG, "Fan resumed. Fan speed changed from:%d to:%d", fan.m_speed_before_pause, fan.m_speed);

  return fan_set_speed(fan.m_speed);
}


unsigned int  fan_get_speed(){
  return fan.m_speed;
}