#ifndef GLOBAL_H
#define GLOBAL_H


//components initialized?
//battery, BLE, blink, fan, http_log, mqtt, open_lid, wifi



//statuses
//wifi connected, watchdog timers?



//MQTT messages
//define MQTT_send
#define MQTT_NAME "flame_thrower"
#define LAST_UPDATED_TOPIC MQTT_NAME "/last_updated"

#define FLAME_THROWER_CONNECTED	MQTT_NAME "/connected"
#define FAN_STATE_TOPIC			MQTT_NAME "/state"
#define FAN_SPEED_TOPIC			MQTT_NAME "/percentage"
#define FAN_FREQUENCY_TOPIC_SET	MQTT_NAME "/set/frequency"
#define FAN_FREQUENCY_TOPIC		MQTT_NAME "/frequency"

#define BLINK_INTERVAL			MQTT_NAME "/blink"

#define LID_ACTIVE_GET			MQTT_NAME "/lid"
#define LID_OPEN				MQTT_NAME "/lid_open"
#define CALIBRATION_GET			MQTT_NAME "/calibration"

#define FAN_SUBSCRIPTION		MQTT_NAME "/set/#"
#define BLINK_INTERVAL_SET		MQTT_NAME "/set/blink"
#define FAN_SPEED_TOPIC_SET		MQTT_NAME "/set/percentage"
#define FAN_SPEED_TOPIC_ON_OFF	MQTT_NAME "/set/command"
#define LID_ACTIVE_SET			MQTT_NAME "/set/lid"
#define OTA_COMMAND_TOPIC		MQTT_NAME "/set/ota"
#define CALIBRATION_SET			MQTT_NAME "/set/calibration"




//global variables
//timers times
//wifi credentials, BT name
//fan pins, battery pin, blink pin, open_lid pin


#define EXAMPLE_ESP_WIFI_SSID       CONFIG_ESP_WIFI_SSID //"YURA_LAPTOP"
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD //"9J36j98$" //






#endif