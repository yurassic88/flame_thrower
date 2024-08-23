#include "mqtt.h"
#include "battery.h"
#include "ota.h"
#include "screen.h"
#include <string.h>

#define TAG_MQTT "MQTT"


static bool MQTTInitialized = false;
static bool MQTTConnected = false;
static esp_mqtt_client_handle_t client;


//const char* mqtt_server = "192.168.1.154";
const char* mqtt_uri = "mqtt://192.168.1.154:1883";
//const int mqtt_port = 1883;
const char* mqtt_username = "mqtt_user";
const char* mqtt_password = "mqtt";

const char* mqtt_client_name = "Flame_thrower";

const char* last_updated_topic = "flame_thrower/last_updated";
const char* flame_thrower_connected = "flame_thrower/connected";
const char* fan_state_topic = "flame_thrower/state";
const char* fan_speed_topic = "flame_thrower/percentage";
const char* fan_speed_topic_set = "flame_thrower/set/percentage";
const char* fan_speed_topic_on_off = "flame_thrower/set/command";
const char* fan_frequency_topic_set = "flame_thrower/set/frequency";
//const char* activate_1kOhmPin = "flame_thrower/set/1kOhm";
const char* fan_frequency_topic = "flame_thrower/frequency";
const char* fan_subscription = "flame_thrower/set/#";
const char* blink_interval_set = "flame_thrower/set/blink";
const char* blink_interval = "flame_thrower/blink";
const char* lid_active_set = "flame_thrower/set/lid";
const char* lid_active_get = "flame_thrower/lid";
const char* lid_open = "flame_thrower/lid_open";
const char* ota_command_topic = "flame_thrower/set/ota";
const char* calibration_set = "flame_thrower/set/calibration";
const char* calibration_get = "flame_thrower/calibration";


bool mqttIsInitialized(){
    return MQTTInitialized;
}

bool mqttIsConnected(){
    return MQTTConnected;
}

esp_mqtt_client_handle_t mqttGetClient(){
    return client;
}

bool mqttReconnect(){
    if(mqttIsInitialized()){
        esp_mqtt_client_reconnect(mqttGetClient());
        return true;
    }
    return false;    
}


static void log_error_if_nonzero(const char *message, int error_code)
{
    if (error_code != 0) {
        ESP_LOGE(TAG_MQTT, "Last error %s: 0x%x", message, error_code);
    }
}


static void processSetMQTT(esp_mqtt_event_handle_t event, esp_mqtt_client_handle_t client){

  char *data = (char*)malloc(event->data_len+1);
  memset(data, 0, event->data_len+1);
  strncpy(data, (char*)event->data, event->data_len);

  ESP_LOGI("FAN", "fan_mqtt: %.*s | %s", event->topic_len, event->topic, data);

  //topic = flame_thrower/set/percentage
  if (strncmp(event->topic, fan_speed_topic_set, event->topic_len) == 0) {
    int fanSpeed = atoi(data);
    fan_set_speed(fanSpeed);
  }

  //topic = flame_thrower/set/command "ON" or "OFF"
  else if  (strncmp(event->topic, fan_speed_topic_on_off, event->topic_len) == 0) {
    if(strcmp(data, "OFF") == 0){
        fan_set_speed(0);
    }
    else{
        fan_set_speed(50);
    }
  }

  //topic = flame_thrower/set/frequency
  else if (strncmp(event->topic, fan_frequency_topic_set, event->topic_len) == 0) {
    int frequency = atoi(data);
    fan_set_frequency(frequency);
  }
  
  //topic = flame_thrower/set/blink
  else if (strncmp(event->topic, blink_interval_set, event->topic_len)==0){
    int interval = atoi(data);
    blink_changeTimerDelay(interval);
  }
  
  //topic = flame_thrower/set/lid
  else if (strncmp(event->topic, lid_active_set, event->topic_len)==0){
    bool bActive =  (bool)atoi(data);
    lid_setActive(bActive);
  }

  //topic = flame_thrower/set/calibration
  else if (strncmp(event->topic, calibration_set, event->topic_len)==0){
    bool calibration =  (bool)atoi(data);
    batterySetCalibration(calibration);
  }

  //OTA update
  else if  (strncmp(event->topic, ota_command_topic, event->topic_len) == 0) {
    char update[1024]="";
    strncpy(update, event->data, event->data_len);
    ota_update(update);
  }

  ESP_LOGI("FAN", "fan_speed: %d, fan_freq: %d, fan_duty: %d, lid get_active:%d", fan_get_speed(), 
                                                                                  fan_get_frequency(), 
                                                                                  fan_get_duty(), 
                                                                                  lid_getActive());

  //publish all topics
  char tmp[10]="";
  if(fan_get_speed()==0){
    mqtt_publish(fan_state_topic, "OFF", 1);
  }
  else{
    mqtt_publish(fan_state_topic, "ON", 1);
  }
  
  sprintf(tmp,"%d",fan_get_speed());
  mqtt_publish(fan_speed_topic, tmp, 0);
  
  sprintf(tmp,"%d",fan_get_frequency());
  mqtt_publish(fan_frequency_topic, tmp, 0);
  
  sprintf(tmp,"%ld",blink_getInterval());
  mqtt_publish(blink_interval, tmp, 0);
  
  sprintf(tmp,"%d",lid_getActive());
  mqtt_publish(lid_active_get, tmp, 0);

  sprintf(tmp,"%d",batteryGetCalibration());
  mqtt_publish(calibration_get, tmp, 0);
  
  
  //client.publish(fan_speed_topic, String(fan->get_speed()).c_str());
  //client.publish(fan_frequency_topic, String(fan->get_frequency()).c_str());

  free(data);
  
}

void mqtt_publish(char * topic, char* msg, int retain){
    if(mqttIsConnected()){
        esp_mqtt_client_publish(client, flame_thrower_connected, "online", 0,1,1);
        esp_mqtt_client_publish(client, topic, msg, 0,1,retain);
    }

}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int event_id, void *event_data)
{
    ESP_LOGD(TAG_MQTT, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t ) event_data;
    esp_mqtt_client_handle_t client = (esp_mqtt_client_handle_t ) event->client;
    int msg_id;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:

        MQTTConnected = true;
        scr_set_home_icon(true);

        ESP_LOGI(TAG_MQTT, "[%d]MQTT_EVENT_CONNECTED", xPortGetCoreID());
        msg_id = esp_mqtt_client_publish(client, "flame_thrower/connected", "online", 0, 1, 0);
        ESP_LOGI(TAG_MQTT, "sent publish successful, msg_id=%d", msg_id);
        
        msg_id = esp_mqtt_client_subscribe(client, "flame_thrower/set/#", 0);
        ESP_LOGI(TAG_MQTT, "sent subscribe successful, msg_id=%d", msg_id);

//        msg_id = esp_mqtt_client_subscribe(client, "/topic/qos1", 1);
//        ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);

//        msg_id = esp_mqtt_client_unsubscribe(client, "/topic/qos1");
//        ESP_LOGI(TAG, "sent unsubscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        MQTTConnected = false;
        scr_set_home_icon(false);
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_DISCONNECTED");
        //reconnecting upon disconnect
        mqttReconnect();
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
//        msg_id = esp_mqtt_client_publish(client, "/topic/qos0", "data", 0, 0, 0);
//        ESP_LOGI(TAG, "sent publish successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        //ESP_LOGI(TAG_MQTT, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG_MQTT, "[%d]MQTT_EVENT_DATA", xPortGetCoreID());
        processSetMQTT(event, client);
        
        //printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        //printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG_MQTT, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            log_error_if_nonzero("reported from esp-tls", event->error_handle->esp_tls_last_esp_err);
            log_error_if_nonzero("reported from tls stack", event->error_handle->esp_tls_stack_err);
            log_error_if_nonzero("captured as transport's socket errno",  event->error_handle->esp_transport_sock_errno);
            ESP_LOGI(TAG_MQTT, "Last errno string (%s)", strerror(event->error_handle->esp_transport_sock_errno));

        }
        break;
    default:
        ESP_LOGI(TAG_MQTT, "Other event id:%d", event->event_id);
        break;
    }
}




void mqttInit(){
    if(mqttIsInitialized()){
        mqttReconnect();
        return;
    }
    
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = mqtt_uri,

        .credentials.username = mqtt_username,
        .credentials.set_null_client_id=false,
        .credentials.client_id = "flame_thrower",
        .credentials.authentication.password = mqtt_password,
        .session.last_will.topic = "flame_thrower/connected",
        .session.last_will.msg = "offline",
        .session.last_will.retain = false,
        .session.last_will.msg_len = strlen("offline"),
        .session.disable_clean_session=true,
        .network.refresh_connection_after_ms=1000*60*60, //refresh every 1hr
        };

    client = esp_mqtt_client_init(&mqtt_cfg);
        /* The last argument may be used to pass data to the event handler, in this example mqtt_event_handler */
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, (esp_event_handler_t)mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    MQTTInitialized = true;

}