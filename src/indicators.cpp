#include "indicators.h"
#include <Arduino.h>
#include "indicator_pattern.h"

bool Indicators::begin() {
  if (!digitalPinCanOutput(config::kLedPin)) return false;
  if (config::kBuzzerPin >= 0 && !digitalPinCanOutput(config::kBuzzerPin)) return false;
  pinMode(config::kLedPin, OUTPUT);
  digitalWrite(config::kLedPin, LOW);
  if (config::kBuzzerPin >= 0) {
    pinMode(config::kBuzzerPin, OUTPUT);
    digitalWrite(config::kBuzzerPin, LOW);
  }
  on_ = false;
  return true;
}

void Indicators::update(const ActuatorFsm& fsm, uint32_t now) {
  const bool next = fsm.state() == ActuatorState::ACTUATED ? fsm.pulseActive() :
      indicatorOn(fsm.state(), now - fsm.stateSince());
  if (next == on_) return;
  on_ = next;
  digitalWrite(config::kLedPin, on_ ? HIGH : LOW);
  if (config::kBuzzerPin >= 0) digitalWrite(config::kBuzzerPin, on_ ? HIGH : LOW);
}
