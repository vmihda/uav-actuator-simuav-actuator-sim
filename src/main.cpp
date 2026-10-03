#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <cstdio>
#include <cstring>
#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#endif

#include "button_monitor.h"
#include "config.h"
#include "device_info.h"
#include "event_log.h"
#include "fsm.h"
#include "indicators.h"
#include "power_monitor.h"
#include "pwm_decoder.h"
#include "pwm_input.h"
#include "self_test.h"
#include "settings.h"
#include "settings_file.h"
#include "simulated_sensor.h"
#include "uart_handler.h"
#include "web_server.h"

#ifndef ENABLE_USB_COMMANDS
#define ENABLE_USB_COMMANDS 0
#endif

namespace {
ActuatorFsm fsm;
EventLog eventLog;
Indicators indicators;
Settings settings{};
bool settingsOk = false;
char settingsError[96] = "not loaded";
SimulatedSensor supplySensor("supply_mv", 0);
PowerMonitor powerMonitor;
PwmCommandDecoder pwmDecoder;
ButtonMonitor button;
bool pwmReady = false;
bool dispatchCommand(Command command, uint32_t now);
UartHandler flightUart(Serial2, fsm, dispatchCommand);
#ifdef ARDUINO
CommandResult dispatchWebCommand(Command command, uint32_t now);
StatusSnapshot webStatus();
ActuatorWebServer web(fsm, dispatchWebCommand, webStatus);
QueueHandle_t httpCommands = nullptr;
TaskHandle_t httpTask = nullptr;
portMUX_TYPE statusMux = portMUX_INITIALIZER_UNLOCKED;
StatusSnapshot publishedStatus{ActuatorState::POST, ErrorCode::None, 0, false, 0};
#else
CommandResult dispatchLocalWebCommand(Command command, uint32_t now) {
  return dispatchCommand(command, now) ? CommandResult::Accepted : CommandResult::Rejected;
}
ActuatorWebServer web(fsm, dispatchLocalWebCommand);
#endif
#if ENABLE_USB_COMMANDS
// The USB UART is initialized by setup; only the test build accepts commands.
bool setSimulatedSupply(int32_t millivolts);
UartHandler usbConsole(Serial, fsm, dispatchCommand, setSimulatedSupply);
#endif
bool webReady = false;
bool watchdogReady = false;
uint32_t lastDebugAt = 0;
ActuatorState reportedState = ActuatorState::POST;
ErrorCode reportedError = ErrorCode::None;
PowerVerdict reportedPower = PowerVerdict::Ok;
PwmBand reportedBand = PwmBand::None;
bool reportedCaptured = false;
bool reportedNeutral = false;
bool faultSeen = false;

void logLine(const char* line) { Serial.println(line); }

const char* commandName(Command command) {
  switch (command) {
    case Command::Start: return "START";
    case Command::Stop: return "STOP";
    case Command::Deploy: return "DEPLOY";
    case Command::Status: return "STATUS";
    case Command::Invalid: return "INVALID";
  }
  return "INVALID";
}

#if ENABLE_USB_COMMANDS
bool setSimulatedSupply(int32_t millivolts) {
  if (!supplySensor.isSimulated()) return false;
  supplySensor.set(millivolts);
  return true;
}
#endif

#ifdef ARDUINO
void publishStatus(uint32_t now) {
  const auto snapshot = fsm.snapshot(now);
  portENTER_CRITICAL(&statusMux);
  publishedStatus = snapshot;
  portEXIT_CRITICAL(&statusMux);
}

StatusSnapshot webStatus() {
  portENTER_CRITICAL(&statusMux);
  const auto snapshot = publishedStatus;
  portEXIT_CRITICAL(&statusMux);
  return snapshot;
}

void serveHttp(void*) {
  for (;;) {
    web.update();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

CommandResult dispatchWebCommand(Command command, uint32_t now) {
  // Only this one HTTP task produces requests. The main loop alone owns the FSM.
  static uint32_t nextId = 0;
  nextId = nextId % 0x7fffffffUL + 1;
  const HttpCommand request{command, now, nextId};
  // The main loop checks freshness with its own clock, so a request that times
  // out here may still execute; report it as unconfirmed, never as rejected.
  if (xQueueSend(httpCommands, &request, 0) != pdTRUE) return CommandResult::Unavailable;
  for (;;) {
    const uint32_t remaining = request.remainingMs(millis());
    if (remaining == 0) return CommandResult::Unavailable;
    uint32_t reply = 0;
    const TickType_t waitTicks = (remaining + portTICK_PERIOD_MS - 1) / portTICK_PERIOD_MS;
    if (xTaskNotifyWait(0, UINT32_MAX, &reply, waitTicks) != pdTRUE)
      return CommandResult::Unavailable;
    if ((reply >> 1) == request.id)
      return (reply & 1) != 0 ? CommandResult::Accepted : CommandResult::Rejected;
  }
}

void processHttpCommands(uint32_t now) {
  HttpCommand request;
  if (xQueueReceive(httpCommands, &request, 0) != pdTRUE) return;
  const bool fresh = request.remainingMs(now) > 0;
  const bool accepted = fresh && dispatchCommand(request.command, now);
  publishStatus(millis());
  xTaskNotify(httpTask, (request.id << 1) | (accepted ? 1U : 0U), eSetValueWithOverwrite);
}
#endif

bool startWeb() {
  if (!web.begin()) return false;
#ifdef ARDUINO
  if (!httpCommands) httpCommands = xQueueCreate(1, sizeof(HttpCommand));
  if (!httpCommands) return false;
  // Vendor Stream parsing can spin while awaiting bytes. Idle priority lets
  // IDLE0 run its watchdog hook even during a trickled HTTP header.
  if (!httpTask && xTaskCreatePinnedToCore(serveHttp, "actuator-http", 6144, nullptr,
                                          tskIDLE_PRIORITY, &httpTask, 0) != pdPASS) return false;
#endif
  return true;
}

bool linksHealthy() {
  return static_cast<bool>(Serial2) && webReady && WiFi.getMode() == WIFI_AP && watchdogReady &&
      !fsm.pulseActive();
}

bool buttonRaw() { return settings.buttonPin >= 0 && digitalRead(settings.buttonPin) == LOW; }

void configureInputs() {
  supplySensor.set(settings.powerSimulatedDefaultMv);
  powerMonitor.configure(&supplySensor, settings.powerMinMv, settings.powerMaxMv, settings.powerStableMs);
  pwmDecoder.configure(settings);
  pwmReady = pwmInputBegin(settings.pwmInputPin);
  button.configure(settings.buttonDebounceMs, settings.buttonStuckMs);
  if (settings.buttonPin >= 0) pinMode(settings.buttonPin, INPUT_PULLUP);
}

SelfTestInputs selfTestInputs(bool storageOk, uint32_t now) {
  SelfTestInputs in{};
  in.storageOk = storageOk;
  in.settingsOk = settingsOk;
  in.settingsError = settingsError;
  in.device = readDeviceInfo();
  in.buttonEnabled = settingsOk && settings.buttonPin >= 0;
  in.buttonStuck = in.buttonEnabled && button.stuck(now);
  in.powerVerdict = powerMonitor.verdict();
  in.powerInstant = powerMonitor.instant();
  in.pwmReady = pwmReady;
  in.linksOk = linksHealthy();
  return in;
}

void logDevice(const DeviceInfo& device) {
  char line[96];
  std::snprintf(line, sizeof(line), "DEVICE:MAC=%02X:%02X:%02X:%02X:%02X:%02X,CHIP=%s,REV=%u,FLASH_MB=%lu",
      device.mac[0], device.mac[1], device.mac[2], device.mac[3], device.mac[4], device.mac[5],
      device.model, static_cast<unsigned>(device.revision),
      static_cast<unsigned long>(device.flashBytes / (1024UL * 1024UL)));
  logLine(line);
}

ErrorCode bootSelfTest() {
  const bool storageOk = eventLog.begin();
  settingsOk = loadSettingsFile(settings, settingsError, sizeof(settingsError));
  logDevice(readDeviceInfo());
  if (settingsOk) {
    configureInputs();
    // Sample button and supply together, long enough for both persistence windows.
    const uint32_t window = settings.buttonStuckMs > settings.powerStableMs ?
        settings.buttonStuckMs : settings.powerStableMs;
    const uint32_t start = millis();
    for (;;) {
      const uint32_t now = millis();
      powerMonitor.update(now);
      button.update(buttonRaw(), now);
      if (static_cast<uint32_t>(now - start) >= window) break;
      delay(10);
    }
  }
  return runSelfTest(SelfTestMode::Boot, selfTestInputs(storageOk, millis()), logLine);
}

// Recovery reuses boot settings and the running monitors, so it never waits.
ErrorCode recoverySelfTest(uint32_t now) {
  const bool storageOk = eventLog.begin();
  if (!webReady) webReady = startWeb();
  return runSelfTest(SelfTestMode::Recovery, selfTestInputs(storageOk, now), logLine);
}

bool faultable(ActuatorState state) {
  return state == ActuatorState::SAFE || state == ActuatorState::ARMING ||
      state == ActuatorState::ARMED || state == ActuatorState::ACTUATED;
}

void servicePower(uint32_t now) {
  const PowerVerdict verdict = powerMonitor.update(now);
  if (verdict != reportedPower) {
    const SensorReading reading = powerMonitor.reading();
    char line[64];
    std::snprintf(line, sizeof(line), "VIN:%ldmV,VERDICT:%s",
                  reading.valid ? static_cast<long>(reading.value) : -1L, powerVerdictName(verdict));
    logLine(line);
    reportedPower = verdict;
  }
  if (verdict != PowerVerdict::Ok && faultable(fsm.state()))
    fsm.fault(verdict == PowerVerdict::SensorFailure ? ErrorCode::SensorFailure :
              ErrorCode::PowerOutOfRange, now);
}

void servicePwm(uint32_t now) {
  uint16_t widthUs = 0;
  if (pwmInputTake(widthUs)) pwmDecoder.onPulse(widthUs, now);
  if (pwmDecoder.takeHeartbeat()) fsm.heartbeat(now);
  const PwmEvent event = pwmDecoder.update(now);
  if (event.kind == PwmEventKind::Command) {
    char line[32];
    std::snprintf(line, sizeof(line), "PWM:CMD:%s", commandName(event.command));
    logLine(line);
    dispatchCommand(event.command, now);
  } else if (event.kind != PwmEventKind::None) {
    const bool lost = event.kind == PwmEventKind::Lost;
    logLine(lost ? "PWM:LOST" : "PWM:INVALID");
    const ActuatorState state = fsm.state();
    if (state == ActuatorState::ARMING || state == ActuatorState::ARMED)
      fsm.fault(lost ? ErrorCode::PwmLost : ErrorCode::PwmInvalid, now);
  }
  if (pwmDecoder.band() != reportedBand || pwmDecoder.captured() != reportedCaptured ||
      pwmDecoder.neutral() != reportedNeutral) {
    reportedBand = pwmDecoder.band();
    reportedCaptured = pwmDecoder.captured();
    reportedNeutral = pwmDecoder.neutral();
    char line[64];
    std::snprintf(line, sizeof(line), "PWM:%uus,BAND:%s,CAPTURED:%d,NEUTRAL:%d",
                  static_cast<unsigned>(pwmDecoder.widthUs()), pwmBandName(reportedBand),
                  reportedCaptured ? 1 : 0, reportedNeutral ? 1 : 0);
    logLine(line);
  }
}

void serviceButton(uint32_t now) {
  if (settings.buttonPin < 0) return;
  if (button.update(buttonRaw(), now)) {
    logLine("BUTTON:STOP");
    dispatchCommand(Command::Stop, now);
  }
}

// PWM must pass through STOP again after a fault unless it already sits there.
void trackFault() {
  const bool faulted = fsm.state() == ActuatorState::FAULT;
  if (faulted && !faultSeen) pwmDecoder.onFault();
  faultSeen = faulted;
}

void debugStatus(uint32_t now) {
  char line[96];
  if (formatTelemetry(fsm, now, line, sizeof(line))) {
    const std::size_t size = std::strlen(line);
    if (Serial.availableForWrite() >= static_cast<int>(size))
      Serial.write(reinterpret_cast<const uint8_t*>(line), size);
  }
}

bool dispatchCommand(Command command, uint32_t now) {
  fsm.update(now);
  if (command == Command::Stop && fsm.state() == ActuatorState::FAULT) {
    const ErrorCode error = recoverySelfTest(now);
    fsm.completePost(error == ErrorCode::None, now, error);
  }
  if (command == Command::Deploy && fsm.state() == ActuatorState::ARMED) {
    // Persist the event before starting even the simulated output pulse.
    if (!eventLog.recordDeployment(now, fsm.deploymentCount() + 1)) {
      fsm.fault(ErrorCode::StorageFailure, now);
      fsm.completePost(false, now, ErrorCode::StorageFailure);
      return false;
    }
    Serial.printf("EVENT:SIMULATED_DEPLOY,UPTIME_MS:%lu\n", static_cast<unsigned long>(now));
  }
  return fsm.handle(command, now);
}
}  // namespace

void setup() {
  Serial.begin(config::kBaudRate);
  indicators.begin();
  flightUart.begin();
  webReady = startWeb();
  watchdogReady = esp_task_wdt_init(5, true) == ESP_OK;
  if (watchdogReady) {
    enableLoopWDT();
    watchdogReady = esp_task_wdt_status(nullptr) == ESP_OK;
  }
  const ErrorCode error = bootSelfTest();
  const uint32_t now = millis();
  fsm.completePost(error == ErrorCode::None, now, error);
#ifdef ARDUINO
  publishStatus(now);
#endif
  Serial.println("ACTUATOR-SIM: logical output only; no actuator GPIO configured");
  Serial.printf("UART2:RX=%d,TX=%d,BAUD=%lu; ARMING_MS=%lu\n", config::kUartRxPin,
      config::kUartTxPin, static_cast<unsigned long>(config::kBaudRate),
      static_cast<unsigned long>(config::kArmingDelayMs));
  Serial.println("WiFi: ACTUATOR-SIM / http://192.168.4.1");
  debugStatus(now);
  flightUart.sendStatus(now);
}

void loop() {
  fsm.update(millis());
  // Order is a contract: it decides which fault is reported first.
  if (settingsOk) {
    servicePower(millis());
    servicePwm(millis());
    serviceButton(millis());
  }
  flightUart.update(millis());
#if ENABLE_USB_COMMANDS
  usbConsole.update(millis());
#endif
#ifdef ARDUINO
  if (webReady) processHttpCommands(millis());
#else
  if (webReady) web.update();
#endif
  const uint32_t now = millis();
  fsm.update(now);
  trackFault();
  indicators.update(fsm, now);
#ifdef ARDUINO
  publishStatus(now);
#endif
  const bool changed = fsm.state() != reportedState || fsm.error() != reportedError;
  if (changed) {
    debugStatus(now);
    reportedState = fsm.state();
    reportedError = fsm.error();
  }
#if !ENABLE_USB_COMMANDS
  if (static_cast<uint32_t>(now - lastDebugAt) >= config::kTelemetryIntervalMs) {
    debugStatus(now);
    lastDebugAt = now;
  }
#else
  (void)lastDebugAt;
#endif
  yield();
}
