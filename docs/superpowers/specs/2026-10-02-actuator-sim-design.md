# ESP32 actuator simulator

The supplied technical assignment is the approved implementation scope. This is
a software simulation: the actuator output is a timed logical flag, never a
power-switch GPIO. The LED and optional active buzzer show the result.

## Architecture and contracts

- Arduino/PlatformIO, `esp32dev`, UART2 RX16/TX17 at 115200 8N1.
- Portable C++ FSM receives explicit 32-bit timestamps. Unsigned subtraction
  handles millis rollover. POST checks storage, UART, Wi-Fi and inactive
  simulated output. Success enters SAFE; failure enters FAULT.
- START: SAFE → ARMING. After 300 seconds → ARMED. An `esp32dev-test` build
  uses 10 seconds. START elsewhere is rejected without restarting the timer.
- DEPLOY: only ARMED → ACTUATED. Early/repeated requests are rejected. The
  logical pulse lasts 3 seconds; ACTUATED remains latched until STOP.
- STOP: cancels arming, disarms, ends the pulse and returns to SAFE. In FAULT,
  recovery requires successful health checks; persistent POST/storage problems
  remain FAULT. STOP does not erase the deployment journal.
- A 30-second absence of valid commands/status requests from both control
  channels during ARMING/ARMED causes FAULT. Valid status requests are heartbeats.
  Timeout is checked before accepting a new command. SAFE has no watchdog.
- Unknown, malformed or oversized UART lines cause FAULT. A bounded parser
  accepts LF/CRLF and discards an oversized line through the next LF.
- UART telemetry: `STATE:<NAME>,TIME_LEFT:<seconds>,ERR:<id>\n`, every second
  and in response to STATUS. Remaining seconds round up.
- SoftAP ACTUATOR-SIM/password123. GET `/status`; POST `/start`, `/stop`,
  `/deploy`. HTTP 409 for commands disallowed in the current state. JSON reports
  state, countdown, error, pulse, and deployment count. GET control URLs return
  405 without changing state. The page polls every second and disables DEPLOY
  outside ARMED or when a request fails.
- Vendor WebServer runs in a separate idle-priority FreeRTOS task on core 0.
  Main loop owns the FSM, receiving HTTP commands via a bounded one-slot queue
  and publishing protected status snapshots. Slow HTTP parsing cannot block
  UART, pulse cutoff or core-0 idle-watchdog servicing.
- Table-based nonblocking LED/active-buzzer patterns: SAFE 0.5Hz, ARMING 2Hz,
  ARMED 5Hz, ACTUATED on for 3 seconds, FAULT three short flashes then a pause.
- LittleFS is never automatically formatted. Flash the supplied filesystem
  image before first boot. Deployment events append to a bounded journal,
  rotating to one previous file. A journal failure enters FAULT.

## User-authorized hardware integration

ESP32 RX2/TX2 are connected to SpeedyBee F405 V3 UART6, with ArduPilot 4.7.1.
User requested configuring the flight controller and disabling GPS. Set
SERIAL6_PROTOCOL=0 (raw initialized UART), SERIAL6_BAUD=115, SERIAL6_OPTIONS=0,
GPS1_TYPE=0, GPS2_TYPE=0 and reboot while disarmed. Preserve original parameters.
Test the ASCII protocol via MAVLink SERIAL_CONTROL device106. Deploy/STOP remain
ESP32 simulated commands and never arm the flight controller.

## Validation

Host tests use the same FSM, parser and indication logic as firmware, with
deterministic clocks. Cover legal/illegal transitions, STOP recovery, persistent
health failure, watchdog precedence, pulse cutoff, rollover, UART framing and
overflow, telemetry and all indication patterns. Build both ESP32 environments
and LittleFS. A host terminal simulator accepts the actual UART command strings
plus explicit time advance and fault injection. Hardware upload is separate
from source/build verification and must target a known ESP32 device.
