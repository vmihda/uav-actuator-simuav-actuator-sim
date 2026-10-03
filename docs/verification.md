# Verification — 2026-10-02

Implementation is complete for the supplied software-simulation assignment and
the user's subsequent SpeedyBee configuration request.

## Fresh verification results

| Check | Result |
| --- | --- |
| Native firmware contracts, ASan/UBSan, strict warnings | 38/38 passed |
| Actual panel JavaScript behavior | 3/3 passed |
| Deterministic terminal protocol replay | Passed |
| `esp32dev` firmware, 300-second interval | Built successfully |
| `esp32dev-test` firmware, 10-second interval | Built successfully |
| LittleFS image | Built, uploaded, ESP32 POST passed |
| Actual ESP32 USB bench sequence | Passed, finished SAFE |
| Laptop → ArduPilot → UART6 → ESP32 bench sequence | Passed, finished SAFE |
| Independent code review after fixes | No further concrete findings |

Both hardware sequences rejected DEPLOY in SAFE/ARMING, reached ARMED, accepted
one DEPLOY, retained ACTUATED for repeated DEPLOY, returned SAFE on STOP, entered
FAULT/ERR2 for invalid input and recovered with STOP. The deployed application
has no actuator power GPIO.

## Installed hardware configuration

- ESP32-D0WD-V3, USB `/dev/cu.usbserial-A5069RR4`.
- Installed environment: `esp32dev-test`, ARMING 10 seconds, pulse 3 seconds.
- SpeedyBee F405 V3, USB `/dev/cu.usbmodem2101`, ArduPilot 4.7.1.
- Read-back after reboot: SERIAL6_PROTOCOL=0, SERIAL6_BAUD=115,
  SERIAL6_OPTIONS=0, GPS1_TYPE=0, GPS2_TYPE=0.
- Flight controller remained disarmed. SERIAL_CONTROL exclusive access was
  released after testing; persistent passthrough parameters were not changed.
- UART2 ESP32 RX16 receives from T6; TX17 transmits to R6. Successful
  bidirectional tests confirmed the physical command/telemetry path.

## Preserved state

- Previous ESP32 flash: `backups/esp32-before-simulator.bin`, 4,194,304 bytes.
- Backup SHA256:
  `f010a52a55a34c29cc34c396c393d4abd4fec18671c5769102360004f6306046`.
- Original FC parameters: `backups/fc-parameters-before.json`.

## Compiled resources

| Environment | Static RAM | Application flash |
| --- | ---: | ---: |
| esp32dev | 45,312 bytes | 829,621 bytes |
| esp32dev-test | 45,400 bytes | 829,637 bytes |

## Reviewed corrections

- HTTP parsing is isolated from the main FSM/UART/indication loop; the HTTP task
  uses idle priority to keep core-0's idle watchdog serviced.
- Queued HTTP commands have a bounded deadline with rollover-safe subtraction.
- Journal health checks validate the actual journal paths and preserve retained
  event files; only deployment recording rotates files.
- ACTUATED indication uses the latched pulse flag, so it cannot relight after
  a complete `millis()` cycle.

# Verification — 2026-10-03: PWM commands, self-test and sensors

| Check | Result |
| --- | --- |
| Native contracts (ASan/UBSan, strict warnings) | 89/89 passed |
| Panel JavaScript | 8/8 passed |
| Terminal protocol replay | Passed |
| `esp32dev` and `esp32dev-test` firmware, LittleFS image | Built with `-Werror` |
| Boot self-test on the ESP32-D0WD-V3 | All eight checks `OK`, MAC `A4:F0:0F:67:69:EC`, 4 MB flash |
| PWM capture from SpeedyBee M1 on GPIO27 | 1000 µs commanded, 1001 µs measured, STOP band, neutral |
| USB bench sequence (`hardware_smoke.py`) | Passed, finished SAFE |
| PWM bench (`pwm_bench.py`) | Passed: arming, 35 s ARMED on PWM heartbeat only, deploy, STOP, early DEPLOY consumed |

The filesystem was reprovisioned to install `settings.json`. The previous
partition, including 33 deployment-journal records, is preserved in
`backups/littlefs-before-settings.bin` (SHA-256
`1811fa4f080a86cb827172bafde3140a7f326cf39b6057e8b260f3d1be9b5bf5`).
The flight controller stayed disarmed; M1 was left at 1000 µs, which is not
persisted across a flight-controller reboot.
