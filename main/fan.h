#ifndef FAN_H
#define FAN_H

//#include "driver/ledc.h"
#include <stdbool.h>

#define FAN_PIN             4 //25
#define A_IN_PIN_1          12 //32
#define A_IN_PIN_2          33 //33
#define STDBY               32 //26

#define GPIO_OUTPUT_PIN_SEL  ((1ULL<<FAN_PIN) | (1ULL<<A_IN_PIN_1) | (1ULL<<A_IN_PIN_2) | (1ULL<<STDBY))


typedef struct {

  unsigned int  m_gpio;
  
  unsigned int  m_aIn1_pin;
  bool          m_aIn1_pos;
  unsigned int  m_aIn2_pin;
  bool          m_aIn2_pos;
  unsigned int  m_standby_pin;
  bool          m_standby_pos;
  
  unsigned int  m_channel;
  unsigned int  m_frequency;
  unsigned int  m_duty;
  unsigned int  m_speed; //0-100
  unsigned int  m_resolution; //8bit
  bool          m_initiated;

  unsigned int  m_speed_before_pause;
  
}FAN;

  bool          fan_init(unsigned int gpio, unsigned int aIn1, unsigned int aIn2, unsigned int stand_by, unsigned int start_speed);
  
  bool          fan_set_speed(unsigned int speed);
  bool          fan_pause();
  bool          fan_resume();
  bool          fan_set_stand_by(bool stand_by);
  bool          fan_in_pins_reverse();
  
  bool          fan_set_frequency(unsigned int frequency);
  unsigned int  fan_get_frequency();

  unsigned int  fan_get_duty();
  unsigned int  fan_get_speed();

  unsigned int  fan_get_channel();
  bool          fan_set_duty(unsigned int duty); 

#endif