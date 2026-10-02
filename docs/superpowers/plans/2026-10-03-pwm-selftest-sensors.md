# PWM commands, self-test and sensors — implementation plan

**Spec:** [2026-10-03-pwm-selftest-sensors-design.md](../specs/2026-10-03-pwm-selftest-sensors-design.md)

Every task follows TDD (failing host test → implementation → `python3 scripts/test_native.py`
green → commit). Pure modules have no Arduino dependencies and are compiled by
`scripts/test_native.py`; hardware adapters get host fakes in `test/support/`.

## Global constraints

- C++11, `-Wall -Wextra -Werror -pedantic`, ASan/UBSan on host; firmware builds with `-Werror`.
- ArduinoJson 7.4.3 single header vendored at `lib/ArduinoJson/src/ArduinoJson.h`
  (SHA-256 `ab5fbb8268b846b5f4bc5a5fee11bb2c96f7b8b846f5bef6540afb6a9cc76a5b`), included with `-isystem` on host.
- UART telemetry format, LED patterns and the arming-delay build flag do not change.
- Unsigned subtraction for every timestamp comparison (millis rollover).

## Review focus

1. PWM widths exactly on band edges are inside the band (inclusive bounds).
2. Timers in decoder, power and button monitors survive millis rollover.
3. Settings values of the wrong JSON type (`"27"`, `27.5`) are rejected, not coerced.
4. A PWM band held shorter than `stable_ms` never commands.
5. A button press shorter than `stuck_ms` during the boot window is not `ButtonStuck`.

## Tasks

### 1. Test harness and FSM additions
- Move `CHECK`/`test()` from `test/native/test_main.cpp` into `test/native/harness.h`
  so new test files (`test/native/test_<module>.cpp`, each exposing `void run<Module>Tests()`) share it.
- `ErrorCode` gains 6 `SettingsInvalid`, 7 `ButtonStuck`, 8 `PwmInvalid`, 9 `PwmLost`,
  10 `PowerOutOfRange`, 11 `SensorFailure`.
- `void ActuatorFsm::heartbeat(uint32_t now)`: runs `update(now)` first, then refreshes the control deadline.
- Tests: heartbeat keeps ARMING alive to ARMED; a heartbeat after an expired deadline does not clear the timeout.

### 2. Settings
- Vendor ArduinoJson; add `-isystem lib/ArduinoJson/src` to `scripts/test_native.py`.
- `include/settings.h`: `struct PwmRange {uint16_t minUs, maxUs;}`, `struct Settings` (spec fields),
  `bool parseSettings(const char* json, size_t length, Settings& out, char* error, size_t capacity)`.
- `include/settings_file.h`: `bool loadSettingsFile(Settings&, char* error, size_t capacity)` reading `/settings.json` (≤ 2048 bytes).
- `data/settings.json` with the spec defaults.
- Tests: shipped file parses; missing field, unknown key, wrong type, every ordering rule,
  reserved/strapping/no-pull-up pins, button `-1`, invalid JSON, small error buffer; loader missing/valid file.
- Spec fix: ArduinoJson reports the error kind, not an offset.

### 3. PWM decoder
- `include/pwm_decoder.h`: `enum class PwmBand {None, Invalid, Dead, Stop, Start, Deploy}`,
  `PwmEvent {PwmEventKind kind; Command command;}`, `PwmCommandDecoder` with
  `configure(const Settings&)`, `onPulse(uint16_t widthUs, uint32_t nowMs)`, `PwmEvent update(uint32_t nowMs)`,
  `onFault()`, `bool takeHeartbeat()`, `captured()`, `neutral()`, `band()`, `widthUs()`; `pwmBandName()`.
- Tests: spec rules 1–7 plus review focus 1, 2 and 4.

### 4. Sensor and power monitor
- `include/sensor.h` (interface), `include/simulated_sensor.h`, `include/power_monitor.h`:
  `enum class PowerVerdict {Ok, OutOfRange, SensorFailure}`, `PowerMonitor::configure(Sensor*, minMv, maxMv, stableMs)`,
  `update(now)`, `verdict()`, `instant()`, `reading()`.
- Tests: persistence, brief dip ignored, inclusive bounds, invalid reading, missing sensor, rollover.

### 5. Button monitor
- `include/button_monitor.h`: `configure(debounceMs, stuckMs)`, `bool update(bool rawPressed, uint32_t now)`
  (true once per debounced press), `pressed()`, `stuck(now)`.
- Tests: bounce ignored, one event per press, stuck after `stuck_ms`, short press not stuck, rollover.

### 6. Self-test evaluator
- `include/device_info.h`: `struct DeviceInfo`, `DeviceInfo readDeviceInfo()`.
- `include/self_test.h`: `SelfTestMode {Boot, Recovery}`, `SelfTestInputs`, `bool chipHealthy(const DeviceInfo&, const char*& detail)`,
  `ErrorCode runSelfTest(SelfTestMode, const SelfTestInputs&, void (*log)(const char*))`.
- `config.h`: `kMinFreeHeapBytes = 64 KiB`, `kMinFlashBytes = 4 MiB`.
- Tests: all-OK log lines, first failure wins, settings failure skips dependent checks,
  recovery skips CHIP, each failure code, chip detail cases.

### 7. USB `SIM:VIN` and panel error names
- `UartLineParser(bool allowSimulation = false)`, `ParseKind::SimulateVin`, `ParseResult::value`.
- `UartHandler` optional `bool (*)(int32_t millivolts)` handler; replies `SIM:VIN:<mV>:OK|REJECTED`.
- Panel error names for codes 6–11.
- Tests: default parser rejects `SIM:VIN`, valid/invalid forms, handler reply, not a heartbeat; panel shows code 9 name.

### 8. Hardware adapters and main-loop integration
- `src/pwm_input.cpp` (GPIO any-edge ISR, `esp_timer_get_time`, spinlock hand-off) and
  `src/device_info.cpp` (eFuse MAC, `esp_chip_info`, `esp_flash_get_size`, free heap);
  host fakes `test/support/pwm_input_fake.cpp`, `test/support/device_info_fake.cpp`;
  `Arduino.h` fake gains `INPUT`, `INPUT_PULLUP`, `digitalRead`, `delay` (advances `fakeNow`).
- `src/main.cpp`: boot self-test with the 1 s sampling window, recovery self-test replacing `healthCheck()`,
  loop order power → PWM → button → UART/USB → HTTP → FSM, decoder `onFault()` on entering FAULT, USB diagnostics.
- Tests: existing runtime test reworked for settings; PWM-only arming with 300 s heartbeat, deployment,
  STOP and `PwmLost`; supply fault via simulated sensor. Both firmware environments build.

### 9. Bench script, README and hardware
- `scripts/pwm_bench.py` (spec sequence through `DO_SET_SERVO`, passive USB telemetry).
- README: wiring, settings, fault codes, self-test log, `SIM:VIN`, one-time filesystem provisioning.
- Hardware (user confirms the filesystem upload first): upload filesystem and test firmware,
  run `pwm_bench.py` and `hardware_smoke.py`.
