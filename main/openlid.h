#ifndef OPENLID_H
#define OPENLID_H

#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "fan.h"
#include "mqtt.h"


void lid_init(bool b_lid_monitor_active);
void lid_setActive(bool paramActive);
bool lid_getActive();
bool lid_get_status();


#endif