#include "uart_handler.h"
#include <cstdio>
#include <cstring>

bool UartHandler::begin() {
  port_.setRxBufferSize(256);
  port_.begin(config::kBaudRate, SERIAL_8N1, config::kUartRxPin, config::kUartTxPin);
  return static_cast<bool>(port_);
}

void UartHandler::update(uint32_t now) {
  // A continuous UART stream must not starve HTTP, timers or indications.
  for (unsigned processed = 0; processed < 128 && port_.available() > 0; ++processed) {
    const int byte = port_.read();
    if (byte < 0) break;
    const auto result = parser_.feed(static_cast<char>(byte));
    switch (result.kind) {
      case ParseKind::CommandReady:
        handler_(result.command, now);
        if (result.command == Command::Status) sendStatus(now);
        break;
      case ParseKind::Invalid:
        handler_(Command::Invalid, now);
        break;
      case ParseKind::Overflow:
        fsm_.fault(ErrorCode::UartOverflow, now);
        break;
      case ParseKind::SimulateVin:
      case ParseKind::SimulateContact: {
        const bool accepted = simulation_ != nullptr && simulation_(result.kind, result.value);
        char reply[40];
        std::snprintf(reply, sizeof(reply), "SIM:%s:%ld:%s\n",
                      result.kind == ParseKind::SimulateVin ? "VIN" : "CONTACT",
                      static_cast<long>(result.value), accepted ? "OK" : "REJECTED");
        const std::size_t length = std::strlen(reply);
        if (port_.availableForWrite() >= static_cast<int>(length))
          port_.write(reinterpret_cast<const uint8_t*>(reply), length);
        break;
      }
      case ParseKind::None:
        break;
    }
  }
  if (static_cast<uint32_t>(now - lastTelemetryAt_) >= config::kTelemetryIntervalMs) {
    sendStatus(now);
    lastTelemetryAt_ = now;
  }
}

void UartHandler::sendStatus(uint32_t now) {
  char line[96];
  if (!formatTelemetry(fsm_, now, line, sizeof(line))) return;
  const std::size_t length = std::strlen(line);
  // Skip this sample when TX is full instead of waiting on the receiver.
  if (port_.availableForWrite() >= static_cast<int>(length))
    port_.write(reinterpret_cast<const uint8_t*>(line), length);
}
