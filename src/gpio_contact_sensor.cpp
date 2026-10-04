#include "gpio_contact_sensor.h"

#include <Arduino.h>

void GpioContactSensor::begin(int pin) {
  pin_ = pin;
  pinMode(pin_, INPUT_PULLUP);
}

SensorReading GpioContactSensor::read(uint32_t) {
  if (pin_ < 0) return {false, 0};
  return {true, digitalRead(pin_) == LOW ? 1 : 0};
}
