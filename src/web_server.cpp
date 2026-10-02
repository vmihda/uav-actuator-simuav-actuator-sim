#include "web_server.h"
#include <Arduino.h>
#include <WiFi.h>
#include <cstdio>
#include "web_page.h"

bool ActuatorWebServer::begin() {
  // Retries after a later startup step fails must not register routes twice.
  if (started_) return true;
  if (!WiFi.mode(WIFI_AP) || !WiFi.softAP(config::kApSsid, config::kApPassword))
    return false;
  server_.on("/", HTTP_GET, [this] {
    server_.sendHeader("Cache-Control", "no-store");
    server_.send_P(200, "text/html; charset=utf-8", kWebPage);
  });
  server_.on("/status", HTTP_GET, [this] {
    handler_(Command::Status, millis());
    sendStatus(200, true);
  });
  server_.on("/start", HTTP_POST, [this] { runCommand(Command::Start); });
  server_.on("/stop", HTTP_POST, [this] { runCommand(Command::Stop); });
  server_.on("/deploy", HTTP_POST, [this] { runCommand(Command::Deploy); });
  server_.onNotFound([this] {
    if (server_.uri() == "/start" || server_.uri() == "/stop" ||
        server_.uri() == "/deploy" || server_.uri() == "/status") {
      server_.sendHeader("Allow", server_.uri() == "/status" ? "GET" : "POST");
      server_.send(405, "application/json", "{\"error\":\"method_not_allowed\"}");
    } else {
      server_.send(404, "application/json", "{\"error\":\"not_found\"}");
    }
  });
  server_.begin();
  started_ = true;
  return true;
}

void ActuatorWebServer::update() { server_.handleClient(); }

void ActuatorWebServer::sendStatus(int code, bool accepted) {
  const StatusSnapshot snapshot = status_ ? status_() : fsm_.snapshot(millis());
  char response[256];
  const int size = std::snprintf(response, sizeof(response),
      "{\"state\":\"%s\",\"time_left\":%lu,\"err\":%u,"
      "\"pulse_active\":%s,\"deployment_count\":%lu,\"accepted\":%s,"
      "\"arming_seconds\":%lu,\"simulation\":true}",
      ActuatorFsm::stateName(snapshot.state),
      static_cast<unsigned long>(snapshot.timeLeftSeconds),
      static_cast<unsigned>(snapshot.error), snapshot.pulseActive ? "true" : "false",
      static_cast<unsigned long>(snapshot.deploymentCount), accepted ? "true" : "false",
      static_cast<unsigned long>(config::kArmingDelayMs / 1000));
  server_.sendHeader("Cache-Control", "no-store");
  if (size < 0 || static_cast<std::size_t>(size) >= sizeof(response)) {
    server_.send(500, "application/json", "{\"error\":\"status_overflow\"}");
    return;
  }
  server_.send(code, "application/json", response);
}

void ActuatorWebServer::runCommand(Command command) {
  const CommandResult result = handler_(command, millis());
  if (result == CommandResult::Unavailable) {
    // A late reply is not a rejection: the controller may still execute it.
    server_.sendHeader("Cache-Control", "no-store");
    server_.send(503, "application/json", "{\"error\":\"command_outcome_unknown\"}");
    return;
  }
  const bool accepted = result == CommandResult::Accepted;
  sendStatus(accepted ? 200 : 409, accepted);
}
