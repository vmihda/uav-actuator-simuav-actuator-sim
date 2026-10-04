#pragma once

#include "sensor.h"

// Contact whisker: a normally open switch from the pin to GND with the
// internal pull-up. Reads 1 when closed (pin low) and 0 when open.
class GpioContactSensor : public Sensor {
 public:
  explicit GpioContactSensor(const char* name) : name_(name) {}
  void begin(int pin);
  const char* name() const override { return name_; }
  bool isSimulated() const override { return false; }
  SensorReading read(uint32_t nowMs) override;

 private:
  const char* name_;
  int pin_ = -1;
};
