#ifndef BLINK_H
#define BLINK_H


void blink_startTimer(uint32_t start_interval);
void blink_turn_off();
void blink_turn_on();
void blink_changeTimerDelay(uint32_t xNewPeriod);
uint32_t blink_getInterval();

#endif