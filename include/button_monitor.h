#pragma once

#include <cstdint>

// Debounces a button and detects one held down for too long.
class ButtonMonitor {
 public:
  void configure(uint32_t debounceMs, uint32_t stuckMs);
  // Returns true once per debounced press.
  bool update(bool rawPressed, uint32_t nowMs);
  bool pressed() const { return pressed_; }
  bool stuck(uint32_t nowMs) const;

 private:
  uint32_t debounceMs_ = 50;
  uint32_t stuckMs_ = 500;
  bool raw_ = false;
  uint32_t rawSince_ = 0;
  bool pressed_ = false;
  uint32_t pressedSince_ = 0;
};
