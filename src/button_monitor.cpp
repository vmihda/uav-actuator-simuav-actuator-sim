#include "button_monitor.h"

void ButtonMonitor::configure(uint32_t debounceMs, uint32_t stuckMs) {
  debounceMs_ = debounceMs;
  stuckMs_ = stuckMs;
}

bool ButtonMonitor::update(bool rawPressed, uint32_t nowMs) {
  if (rawPressed != raw_) {
    raw_ = rawPressed;
    rawSince_ = nowMs;
  }
  if (raw_ == pressed_ || static_cast<uint32_t>(nowMs - rawSince_) < debounceMs_) return false;
  pressed_ = raw_;
  if (pressed_) pressedSince_ = rawSince_;
  return pressed_;
}

bool ButtonMonitor::stuck(uint32_t nowMs) const {
  return pressed_ && static_cast<uint32_t>(nowMs - pressedSince_) >= stuckMs_;
}
