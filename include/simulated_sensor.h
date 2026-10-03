#pragma once

#include "sensor.h"

// Sensor whose value is set by software (bench command or tests).
class SimulatedSensor : public Sensor {
 public:
  SimulatedSensor(const char* name, int32_t value) : name_(name), value_(value) {}
  const char* name() const override { return name_; }
  bool isSimulated() const override { return true; }
  SensorReading read(uint32_t) override { return {valid_, value_}; }
  void set(int32_t value) {
    value_ = value;
    valid_ = true;
  }
  void setInvalid() { valid_ = false; }

 private:
  const char* name_;
  int32_t value_;
  bool valid_ = true;
};
