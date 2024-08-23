/*
 * SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
//#include "freertos/portmacro.h"
#include "hal/adc_types.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "mqtt.h"
#include "battery.h"
#include "screen.h"

const static char *TAG = "ADC_READ";

static bool battery_initialized;

/*---------------------------------------------------------------
        ADC General Macros
---------------------------------------------------------------*/
#define EXAMPLE_ADC1_CHAN0          ADC_CHANNEL_0

#define EXAMPLE_ADC_ATTEN           ADC_ATTEN_DB_12



static int adc_raw[2][10];
static int voltage[2][10];
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle);
static void example_adc_calibration_deinit(adc_cali_handle_t handle);

static bool do_calibration;
static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t adc1_cali_chan0_handle;
void batteryTimer();


/**
 * @brief activate/deactivate calibration
 * 
 */
void batterySetCalibration(bool setCalibration){
    do_calibration = setCalibration;
}

bool batteryGetCalibration(){
    return do_calibration;
}

void battery_init()
{
    //if already initialized
    if(battery_initialized)return;

  //-------------ADC1 Init---------------//
    adc_oneshot_unit_init_cfg_t init_config1 = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    //-------------ADC1 Config---------------//
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_11,
        .atten = EXAMPLE_ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, EXAMPLE_ADC1_CHAN0, &config));

    //-------------ADC1 Calibration Init---------------//
    adc1_cali_chan0_handle = NULL;
    //adc_cali_handle_t adc1_cali_chan1_handle = NULL;
    do_calibration = example_adc_calibration_init(ADC_UNIT_1, EXAMPLE_ADC1_CHAN0, EXAMPLE_ADC_ATTEN, &adc1_cali_chan0_handle);
    
    battery_initialized=true;
    
    xTaskCreate(batteryTimer, "HTTP", 1024*4, NULL, 2, NULL);

}

void bettery_deinit()
{
    //if not initialized
    if(!battery_initialized)return;

    //Tear Down
    ESP_ERROR_CHECK(adc_oneshot_del_unit(adc1_handle));
    if (do_calibration) {
        example_adc_calibration_deinit(adc1_cali_chan0_handle);
    }
}


void batteryTimer(){
    while(1){
        char tmp[10]="";
        sprintf(tmp,"%d", battery_get_voltage());
        mqtt_publish("flame_thrower/voltage", tmp, 0);
        scr_set_battery_icon(battery_get_voltage());
        ESP_LOGI(TAG, "calibration is: %d, voltage is: %d", batteryGetCalibration(), battery_get_voltage());
        vTaskDelay(5*60000/portTICK_PERIOD_MS);
    }
}


int map(int x, int in_min, int in_max, int out_min, int out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


int battery_get_voltage(void)
{
    //if not initialized
    if(!battery_initialized)return -1;

    int average_voltage = 0;
    int sample_num      = 100;

    for (int i=0; i < sample_num; i++){
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, EXAMPLE_ADC1_CHAN0, &adc_raw[0][0]));
        if (do_calibration) {
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc1_cali_chan0_handle, adc_raw[0][0], &voltage[0][0]));
            average_voltage += voltage[0][0];
        }
        else{
            average_voltage += adc_raw[0][0];
        }
        
        vTaskDelay(1/portTICK_PERIOD_MS);
    }

    average_voltage = average_voltage/sample_num;
    return map(average_voltage, 900, 1200, 0, 100);
}

/*---------------------------------------------------------------
        ADC Calibration
---------------------------------------------------------------*/
static bool example_adc_calibration_init(adc_unit_t unit, adc_channel_t channel, adc_atten_t atten, adc_cali_handle_t *out_handle)
{
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Curve Fitting");
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = unit,
            .chan = channel,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (!calibrated) {
        ESP_LOGI(TAG, "calibration scheme version is %s", "Line Fitting");
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = unit,
            .atten = atten,
            .bitwidth = ADC_BITWIDTH_DEFAULT,
        };
        ret = adc_cali_create_scheme_line_fitting(&cali_config, &handle);
        if (ret == ESP_OK) {
            calibrated = true;
        }
    }
#endif

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Calibration Success");
    } else if (ret == ESP_ERR_NOT_SUPPORTED || !calibrated) {
        ESP_LOGW(TAG, "eFuse not burnt, skip software calibration");
    } else {
        ESP_LOGE(TAG, "Invalid arg or no memory");
    }

    return calibrated;
}

static void example_adc_calibration_deinit(adc_cali_handle_t handle)
{
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Curve Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(handle));

#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    ESP_LOGI(TAG, "deregister %s calibration scheme", "Line Fitting");
    ESP_ERROR_CHECK(adc_cali_delete_scheme_line_fitting(handle));
#endif
}