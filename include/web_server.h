#pragma once

#include <WebServer.h>
#include "command_handler.h"

class ActuatorWebServer {
 public:
  using StatusProvider = StatusSnapshot (*)();
  ActuatorWebServer(ActuatorFsm& fsm, CommandHandler handler, StatusProvider status = nullptr)
      : fsm_(fsm), handler_(handler), status_(status), server_(80) {}
  bool begin();
  void update();
 private:
  void sendStatus(int code, bool accepted);
  void runCommand(Command command);
  ActuatorFsm& fsm_;
  CommandHandler handler_;
  StatusProvider status_;
  WebServer server_;
  bool started_ = false;
};
