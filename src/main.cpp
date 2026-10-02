#include <Arduino.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <cstring>
#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#endif

#include "config.h"
#include "event_log.h"
#include "fsm.h"
#include "indicators.h"
#include "uart_handler.h"
#include "web_server.h"

#ifndef ENABLE_USB_COMMANDS
#define ENABLE_USB_COMMANDS 0
#endif

namespace {
ActuatorFsm fsm;
EventLog eventLog;
Indicators indicators;
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
UartHandler usbConsole(Serial, fsm, dispatchCommand);
#endif
bool webReady = false;
bool watchdogReady = false;
uint32_t lastDebugAt = 0;
ActuatorState reportedState = ActuatorState::POST;
ErrorCode reportedError = ErrorCode::None;

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

ErrorCode healthCheck() {
  if (!eventLog.begin()) return ErrorCode::StorageFailure;
  if (!webReady) webReady = startWeb();
  if (!Serial2 || !webReady || WiFi.getMode() != WIFI_AP || !watchdogReady ||
      fsm.pulseActive()) return ErrorCode::SelfTestFailed;
  return ErrorCode::None;
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
    const ErrorCode error = healthCheck();
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
  const bool uartReady = flightUart.begin();
  webReady = startWeb();
  watchdogReady = esp_task_wdt_init(5, true) == ESP_OK;
  if (watchdogReady) {
    enableLoopWDT();
    watchdogReady = esp_task_wdt_status(nullptr) == ESP_OK;
  }
  const ErrorCode error = uartReady ? healthCheck() : ErrorCode::SelfTestFailed;
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
