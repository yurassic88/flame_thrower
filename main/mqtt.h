#ifndef MQTT_H
#define MQTT_H

#include "mqtt_client.h"
#include "esp_log.h"
#include "fan.h"
#include "blink.h"
#include "openlid.h"
#include "global.h"



void mqttInit();
bool mqttIsInitialized();
bool mqttIsConnected();
bool mqttReconnect();
void mqtt_publish(char * topic, char* msg, int retain);
esp_mqtt_client_handle_t mqttGetClient();


#endif