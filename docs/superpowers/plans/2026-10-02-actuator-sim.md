# Actuator Simulator Implementation Plan

> Execute inline with superpowers:executing-plans. Keep source changes in this
> existing workspace; it has no Git repository. Do not initialize or commit one.

**Goal:** Implement and verify the supplied ESP32 software-simulation assignment.

**Architecture:** Portable C++ state machine, UART protocol and indication
patterns; thin Arduino adapters for UART, HTTP, LittleFS and LED/buzzer.

**Tech Stack:** ESP32 Arduino 2.0.17, PlatformIO espressif32 7.0.1, C++11,
host clang++, Python standard-library test launcher.

### 1. Portable behavior, test first

Files: `include/config.h`, `include/fsm.h`, `src/fsm.cpp`,
`include/uart_protocol.h`, `src/uart_protocol.cpp`, `include/indicator_pattern.h`,
`src/indicator_pattern.cpp`, `test/native/test_main.cpp`, `scripts/test_native.py`.

- [x] Write assertions for the transition table, health gating, command timeout,
  rollover, pulse cutoff, telemetry, framing and LED timing. Start with inert
  implementations and observe behavior failures.
- [x] Implement only the specified behavior; rerun the same tests.

Key acceptance assertions:

```cpp
fsm.completePost(true, 0);
CHECK(fsm.state() == ActuatorState::SAFE);
CHECK(!fsm.handle(Command::Deploy, 0));
CHECK(fsm.handle(Command::Start, 0));
CHECK(!fsm.handle(Command::Deploy, 9999));
fsm.update(10000);
CHECK(fsm.state() == ActuatorState::ARMED);
CHECK(fsm.handle(Command::Deploy, 10000));
CHECK(fsm.pulseActive());
fsm.update(13000);
CHECK(!fsm.pulseActive());
```

Verification: `rtk proxy python3 scripts/test_native.py`; expect all cases pass.

### 2. Arduino integration

Files: `src/main.cpp`, `include/uart_handler.h`, `src/uart_handler.cpp`,
`include/web_server.h`, `src/web_server.cpp`, `include/web_page.h`,
`include/indicators.h`, `src/indicators.cpp`, `platformio.ini`, `data/info.txt`.

- [x] Connect shared command handling to both UART2 and WebServer.
- [x] Implement health checks and bounded deployment journal in LittleFS.
- [x] Serve a responsive manual-test page and POST-only command endpoints.
- [x] Apply indication patterns without delay; optional active buzzer defaults
  to disabled, no actuator GPIO.
- [x] Build normal and test firmware plus filesystem using installed PlatformIO.

Verification: `rtk proxy python3 scripts/build_firmware.py`; expect successful
`esp32dev`, `esp32dev-test` and filesystem builds.

### 3. Terminal simulator, review and delivery

Files: `tools/simulator.cpp`, `scripts/simulator.py`, `README.md`.

- [x] Provide host command replay: `CMD:START`, `CMD:STATUS`, `TICK:10000`,
  `CMD:DEPLOY`, `TICK:3000`, `CMD:STOP`; use the real core and parser.
- [x] Document UART connections, Wi-Fi, first filesystem upload, test/normal
  timing, error codes, heartbeat and fault recovery.
- [x] Request read-only code review against the supplied assignment and design.
- [x] Fix concrete findings with regression tests; run fresh final verification.
