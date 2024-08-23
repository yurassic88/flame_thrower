#ifndef BATTERY_H
#define BATTERY_H


void battery_init();
int battery_get_voltage();
void batterySetCalibration(bool setCalibration);
bool batteryGetCalibration();



#endif