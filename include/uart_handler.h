#pragma once

#include <Arduino.h>
#include "command_handler.h"
#include "uart_protocol.h"

using SimulationHandler = bool (*)(int32_t millivolts);

class UartHandler {
 public:
  UartHandler(HardwareSerial& port, ActuatorFsm& fsm, CommandHandler handler,
              SimulationHandler simulation = nullptr)
      : port_(port), fsm_(fsm), handler_(handler), simulation_(simulation),
        parser_(simulation != nullptr) {}
  bool begin();
  void update(uint32_t now);
  void sendStatus(uint32_t now);
 private:
  HardwareSerial& port_;
  ActuatorFsm& fsm_;
  CommandHandler handler_;
  SimulationHandler simulation_;
  UartLineParser parser_;
  uint32_t lastTelemetryAt_ = 0;
};
