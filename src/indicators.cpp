#include "indicators.h"
#include <Arduino.h>
#include "indicator_pattern.h"

void Indicators::begin() {
  pinMode(config::kLedPin, OUTPUT);
  digitalWrite(config::kLedPin, LOW);
  if (config::kBuzzerPin >= 0) {
    pinMode(config::kBuzzerPin, OUTPUT);
    digitalWrite(config::kBuzzerPin, LOW);
  }
  on_ = false;
}

void Indicators::update(const ActuatorFsm& fsm, uint32_t now) {
  const bool next = fsm.state() == ActuatorState::ACTUATED ? fsm.pulseActive() :
      indicatorOn(fsm.state(), now - fsm.stateSince());
  if (next == on_) return;
  on_ = next;
  digitalWrite(config::kLedPin, on_ ? HIGH : LOW);
  if (config::kBuzzerPin >= 0) digitalWrite(config::kBuzzerPin, on_ ? HIGH : LOW);
}
