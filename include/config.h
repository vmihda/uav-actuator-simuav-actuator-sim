#pragma once

#include <cstddef>
#include <cstdint>

#ifndef ARMING_DELAY_MS
#define ARMING_DELAY_MS 300000UL
#endif
#ifndef CONTROL_TIMEOUT_MS
#define CONTROL_TIMEOUT_MS 30000UL
#endif
#ifndef BUZZER_PIN
#define BUZZER_PIN -1
#endif

namespace config {
constexpr int kUartRxPin = 16;
constexpr int kUartTxPin = 17;
constexpr uint32_t kBaudRate = 115200;
constexpr int kLedPin = 2;
constexpr int kBuzzerPin = BUZZER_PIN;  // Optional active buzzer; -1 disables it.
constexpr uint32_t kArmingDelayMs = ARMING_DELAY_MS;
constexpr uint32_t kControlTimeoutMs = CONTROL_TIMEOUT_MS;
constexpr uint32_t kPulseDurationMs = 3000;
constexpr uint32_t kTelemetryIntervalMs = 1000;
constexpr std::size_t kUartLineCapacity = 64;
constexpr std::size_t kJournalMaxBytes = 4096;
constexpr char kApSsid[] = "ACTUATOR-SIM";
constexpr char kApPassword[] = "password123";
static_assert(kArmingDelayMs > 0 && kArmingDelayMs < 0x80000000UL,
              "Arming interval must be positive and less than 2^31 ms");
static_assert(kControlTimeoutMs > 0 && kControlTimeoutMs < 0x80000000UL,
              "Control timeout must be positive and less than 2^31 ms");
}  // namespace config
