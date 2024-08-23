/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdint.h>
//#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/idf_additions.h"
#include "nvs_flash.h"




#include "esp_chip_info.h"
#include "esp_flash_spi_init.h"

#include "fan.h"
#include "mqtt.h"
#include "BLE.h"
#include "blink.h"
#include "portmacro.h"
#include "wifi.h"
#include "http_log.h"
#include "openlid.h"
#include "battery.h"
#include "screen.h"


#define CONFIG_LOG_HTTP_SERVER_URL "http://192.168.1.83:81/esp.php"
#define TAG "MAIN"


void app_main(void)
{
    //return;
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

	lcd_start();

    blink_startTimer(200);

    fan_init(FAN_PIN, A_IN_PIN_1, A_IN_PIN_2, STDBY, 0);

    wifi_init_sta();

    BL_init();

	//lid_init(false);



    int16_t write_to_stdout = 0;
	write_to_stdout = 1;
  	
	//send logs to http
	ESP_ERROR_CHECK(http_logging_init( CONFIG_LOG_HTTP_SERVER_URL, write_to_stdout ));

	ESP_LOGI(TAG, "This is info level");
	ESP_LOGW(TAG, "This is warning level");
	ESP_LOGE(TAG, "This is error level");

	ESP_LOGE(TAG, "This is version through OTA!");

	ESP_LOGI(TAG, "freeRTOS version:%s", tskKERNEL_VERSION_NUMBER);
	ESP_LOGI(TAG, "NEWLIB version:%s", _NEWLIB_VERSION);
	ESP_LOGI(TAG, "lwIP version:%d-%d-%d-%d",
		LWIP_VERSION_MAJOR,LWIP_VERSION_MINOR,
		LWIP_VERSION_REVISION,LWIP_VERSION_RC);
	ESP_LOGI(TAG, "ESP-IDF version:%s", esp_get_idf_version());
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);
	ESP_LOGI(TAG, "chip model is %d, ", chip_info.model);
	ESP_LOGI(TAG, "chip with %d CPU cores, WiFi%s%s",
		chip_info.cores,
		(chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
		(chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");
	ESP_LOGI(TAG, "silicon revision %d", chip_info.revision);

	uint32_t size_flash_chip;
	esp_flash_get_size(NULL, &size_flash_chip);
	ESP_LOGI(TAG, "%"PRIu32"MB %s flash", size_flash_chip / (1024 * 1024),
			(chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");
    
	
	//delay battery init to connect to wifi and send via mqtt
    vTaskDelay(10*1000/portTICK_PERIOD_MS);
    battery_init();

	

	
}


void screen_test(){
	scr_set_auto(false);
	scr_set_home_icon(false);
	scr_set_wifi_icon(false);
	scr_set_bt_icon(false);
	scr_set_battery_icon(100);

	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_auto(true);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_home_icon(true);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_wifi_icon(true);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_bt_icon(true);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_battery_icon(70);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_battery_icon(5);
	vTaskDelay(1000/portTICK_PERIOD_MS);

	scr_set_t(0, 22.5);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_t(1, 23.7);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_t(2, 151.7);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_t(3, 200.7);

	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_pit_t(30.7);

	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_pit_t(250.7);

	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_target_t(100);
	
	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_target_t(300);
	
	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_fan_speed(10);
	vTaskDelay(1000/portTICK_PERIOD_MS);
	scr_set_fan_speed(99);
}
