#ifndef SCREEN_H
#define SCREEN_H

void lcd_start(void);
void scr_set_auto(bool b_auto);
void scr_set_home_icon(bool home_connected);
void scr_set_wifi_icon(bool wifi_connected);
void scr_set_bt_icon(bool bt_connected);
void scr_set_battery_icon(int percentage);
void scr_set_t(int n, float temp);
void scr_set_pit_t(float temp);
void scr_set_target_t(float temp);
void scr_set_fan_speed(int percentage);

#endif