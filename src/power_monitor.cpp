#include "power_monitor.h"

const char* powerVerdictName(PowerVerdict verdict) {
  switch (verdict) {
    case PowerVerdict::Ok: return "OK";
    case PowerVerdict::OutOfRange: return "OUT_OF_RANGE";
    case PowerVerdict::SensorFailure: return "SENSOR_FAILURE";
  }
  return "SENSOR_FAILURE";
}

void PowerMonitor::configure(Sensor* sensor, int32_t minMv, int32_t maxMv, uint32_t stableMs) {
  sensor_ = sensor;
  minMv_ = minMv;
  maxMv_ = maxMv;
  stableMs_ = stableMs;
  instant_ = PowerVerdict::Ok;
  verdict_ = PowerVerdict::Ok;
}

PowerVerdict PowerMonitor::update(uint32_t nowMs) {
  reading_ = sensor_ ? sensor_->read(nowMs) : SensorReading{false, 0};
  const PowerVerdict next = !reading_.valid ? PowerVerdict::SensorFailure :
      (reading_.value < minMv_ || reading_.value > maxMv_) ? PowerVerdict::OutOfRange :
      PowerVerdict::Ok;
  if (next != instant_) {
    // A changed condition restarts the persistence window.
    instant_ = next;
    badSince_ = nowMs;
  }
  if (instant_ == PowerVerdict::Ok) {
    verdict_ = PowerVerdict::Ok;
  } else if (static_cast<uint32_t>(nowMs - badSince_) >= stableMs_) {
    verdict_ = instant_;
  }
  return verdict_;
}
