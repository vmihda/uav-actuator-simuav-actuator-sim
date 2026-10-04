#pragma once

#include <cstddef>
#include <cstdint>

struct PwmRange {
  uint16_t minUs;
  uint16_t maxUs;
};

// Runtime settings from /settings.json; every field is required.
struct Settings {
  int pwmInputPin;
  int buttonPin;  // -1 disables the button.
  int contactPin;  // -1 selects the simulated whisker contact.
  uint16_t pwmValidMinUs;
  uint16_t pwmValidMaxUs;
  PwmRange stop;
  PwmRange start;
  PwmRange deploy;
  uint32_t pwmStableMs;
  uint32_t pwmLossTimeoutMs;
  int32_t powerMinMv;
  int32_t powerMaxMv;
  int32_t powerSimulatedDefaultMv;
  uint32_t powerStableMs;
  uint32_t buttonDebounceMs;
  uint32_t buttonStuckMs;
  uint32_t contactDebounceMs;
  uint32_t contactStuckMs;
  uint32_t contactArmedTimeoutMs;
};

// Parses and strictly validates settings JSON. On failure returns false and
// writes "<field> <reason>" into error (truncated to capacity).
bool parseSettings(const char* json, std::size_t length, Settings& out,
                   char* error, std::size_t capacity);
