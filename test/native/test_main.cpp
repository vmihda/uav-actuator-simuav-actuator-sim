#include <cstring>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>

#include "fsm.h"
#include "indicator_pattern.h"
#include "uart_protocol.h"
#include "uart_handler.h"
#include "web_server.h"
#include "indicators.h"
#include <WiFi.h>
#include <LittleFS.h>
#include "event_log.h"
#include "fixtures.h"
#include "harness.h"

uint32_t fakeNow = 0;
int fakePins[40] = {};
bool fakeInputLow[40] = {};
extern std::deque<uint16_t> fakePwmPulses;
WebServer* WebServer::latest = nullptr;
WiFiClass WiFi;
LittleFSClass LittleFS;
HardwareSerial Serial;
HardwareSerial Serial2;
void setup();
void loop();

int failures = 0;
int cases = 0;
void runFsmHeartbeatTests();
void runSettingsTests();
void runPwmDecoderTests();
void runPowerMonitorTests();
void runButtonMonitorTests();
void runSelfTestTests();
void runSimVinTests();
static ActuatorFsm* commandTarget = nullptr;
bool dispatchTestCommand(Command command, uint32_t now) {
  return commandTarget->handle(command, now);
}
CommandResult dispatchTestWebCommand(Command command, uint32_t now) {
  return commandTarget->handle(command, now) ? CommandResult::Accepted : CommandResult::Rejected;
}
CommandResult unavailableWebCommand(Command, uint32_t) { return CommandResult::Unavailable; }

// Main-loop drivers for integration tests: one 20 ms iteration per step.
void runPwm(uint16_t widthUs, uint32_t durationMs) {
  for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += 20) {
    fakePwmPulses.push_back(widthUs);
    loop();
    fakeNow += 20;
  }
}
void runSilence(uint32_t durationMs) {
  for (uint32_t elapsed = 0; elapsed < durationMs; elapsed += 20) {
    loop();
    fakeNow += 20;
  }
}
// Latest USB telemetry line; reading it, unlike /status, is not a heartbeat.
std::string lastTelemetry() {
  const auto at = Serial.output.rfind("STATE:");
  if (at == std::string::npos) return "";
  return Serial.output.substr(at, Serial.output.find('\n', at) - at);
}

ParseResult feed(UartLineParser& parser, const std::string& line) {
  ParseResult result{ParseKind::None, Command::Invalid, 0};
  for (char byte : line) {
    const auto next = parser.feed(byte);
    if (next.kind != ParseKind::None) result = next;
  }
  return result;
}

int main() {
  test("HTTP command deadline expires without underflow across clock rollover", [] {
    const HttpCommand request{Command::Deploy, UINT32_MAX - 499, 1};
    CHECK(request.remainingMs(request.issuedAt) == 1000);
    CHECK(request.remainingMs(request.issuedAt + 999) == 1);
    CHECK(request.remainingMs(request.issuedAt + 1000) == 0);
    CHECK(request.remainingMs(request.issuedAt + 1001) == 0);
  });
  test("POST health gate", [] {
    ActuatorFsm fsm;
    CHECK(fsm.state() == ActuatorState::POST);
    CHECK(!fsm.pulseActive());
    CHECK(!fsm.handle(Command::Deploy, 0));
    fsm.completePost(true, 0);
    CHECK(fsm.state() == ActuatorState::SAFE);
    CHECK(fsm.error() == ErrorCode::None);
  });
  test("failed POST requires successful health check before STOP", [] {
    ActuatorFsm fsm;
    fsm.completePost(false, 10, ErrorCode::StorageFailure);
    CHECK(fsm.state() == ActuatorState::FAULT);
    CHECK(fsm.error() == ErrorCode::StorageFailure);
    CHECK(!fsm.handle(Command::Stop, 20));
    CHECK(!fsm.handle(Command::Start, 20));
    fsm.completePost(true, 30);
    CHECK(fsm.state() == ActuatorState::FAULT);
    CHECK(fsm.handle(Command::Stop, 30));
    CHECK(fsm.state() == ActuatorState::SAFE);
  });
  test("early DEPLOY never activates; timer rounds up", [] {
    ActuatorFsm fsm(10000);
    fsm.completePost(true, 0);
    CHECK(!fsm.handle(Command::Deploy, 0));
    CHECK(fsm.handle(Command::Start, 0));
    CHECK(fsm.timeLeftSeconds(0) == 10);
    CHECK(fsm.timeLeftSeconds(1) == 10);
    CHECK(fsm.timeLeftSeconds(9001) == 1);
    CHECK(!fsm.handle(Command::Deploy, 9999));
    CHECK(fsm.state() == ActuatorState::ARMING);
    CHECK(!fsm.pulseActive());
    CHECK(fsm.deploymentCount() == 0);
  });
  test("arm deadline then single bounded deployment pulse", [] {
    ActuatorFsm fsm(10000);
    fsm.completePost(true, 0);
    CHECK(fsm.handle(Command::Start, 0));
    fsm.update(10000);
    CHECK(fsm.state() == ActuatorState::ARMED);
    CHECK(fsm.timeLeftSeconds(10000) == 0);
    CHECK(fsm.handle(Command::Deploy, 10000));
    CHECK(fsm.state() == ActuatorState::ACTUATED);
    CHECK(fsm.pulseActive());
    CHECK(fsm.deploymentCount() == 1);
    CHECK(!fsm.handle(Command::Deploy, 11000));
    fsm.update(12999);
    CHECK(fsm.pulseActive());
    fsm.update(13000);
    CHECK(!fsm.pulseActive());
    CHECK(fsm.state() == ActuatorState::ACTUATED);
    CHECK(fsm.deploymentCount() == 1);
  });
  test("STOP cancels timer and subsequent START gets full interval", [] {
    ActuatorFsm fsm(10000);
    fsm.completePost(true, 0);
    fsm.handle(Command::Start, 0);
    CHECK(!fsm.handle(Command::Start, 8000));
    CHECK(fsm.timeLeftSeconds(8000) == 2);
    CHECK(fsm.handle(Command::Stop, 9000));
    fsm.update(20000);
    CHECK(fsm.state() == ActuatorState::SAFE);
    CHECK(fsm.timeLeftSeconds(20000) == 0);
    CHECK(fsm.handle(Command::Start, 20000));
    CHECK(fsm.timeLeftSeconds(20000) == 10);
  });
  test("STOP disarms and cuts a live pulse", [] {
    for (bool deploy : {false, true}) {
      ActuatorFsm fsm(10);
      fsm.completePost(true, 0);
      fsm.handle(Command::Start, 0);
      fsm.update(10);
      if (deploy) fsm.handle(Command::Deploy, 10);
      CHECK(fsm.handle(Command::Stop, 11));
      CHECK(fsm.state() == ActuatorState::SAFE);
      CHECK(!fsm.pulseActive());
    }
  });
  test("watchdog faults during ARMING and ARMED", [] {
    for (uint32_t armingMs : {10000U, 60000U}) {
      ActuatorFsm fsm(armingMs);
      fsm.completePost(true, 0);
      fsm.handle(Command::Start, 0);
      fsm.update(29999);
      CHECK(fsm.state() != ActuatorState::FAULT);
      fsm.update(30000);
      CHECK(fsm.state() == ActuatorState::FAULT);
      CHECK(fsm.error() == ErrorCode::ControlTimeout);
      CHECK(!fsm.pulseActive());
      CHECK(fsm.handle(Command::Stop, 30001));
      CHECK(fsm.error() == ErrorCode::None);
    }
  });
  test("status heartbeat supports full 300 second preparation", [] {
    ActuatorFsm fsm;
    fsm.completePost(true, 0);
    fsm.handle(Command::Start, 0);
    for (uint32_t now = 1000; now <= 300000; now += 1000) {
      CHECK(fsm.handle(Command::Status, now));
      CHECK(fsm.state() != ActuatorState::FAULT);
    }
    CHECK(fsm.state() == ActuatorState::ARMED);
  });
  test("late DEPLOY cannot bypass an expired control deadline", [] {
    ActuatorFsm fsm(10000);
    fsm.completePost(true, 0);
    fsm.handle(Command::Start, 0);
    CHECK(!fsm.handle(Command::Deploy, 30000));
    CHECK(fsm.state() == ActuatorState::FAULT);
    CHECK(fsm.error() == ErrorCode::ControlTimeout);
    CHECK(fsm.deploymentCount() == 0);
  });
  test("SAFE has no control deadline", [] {
    ActuatorFsm fsm;
    fsm.completePost(true, 0);
    fsm.update(900000);
    CHECK(fsm.state() == ActuatorState::SAFE);
  });
  test("invalid input faults; STOP recovers; critical error cuts pulse", [] {
    ActuatorFsm fsm(10);
    fsm.completePost(true, 0);
    CHECK(!fsm.handle(Command::Invalid, 1));
    CHECK(fsm.state() == ActuatorState::FAULT);
    CHECK(fsm.error() == ErrorCode::InvalidCommand);
    CHECK(fsm.handle(Command::Status, 1));
    CHECK(fsm.handle(Command::Stop, 2));
    fsm.handle(Command::Start, 2);
    fsm.handle(Command::Deploy, 12);
    CHECK(fsm.pulseActive());
    fsm.fault(ErrorCode::StorageFailure, 13);
    CHECK(!fsm.pulseActive());
    CHECK(fsm.state() == ActuatorState::FAULT);
  });
  test("arming and pulse deadlines survive millis rollover", [] {
    const uint32_t start = UINT32_MAX - 4999;
    ActuatorFsm fsm(10000);
    fsm.completePost(true, start);
    fsm.handle(Command::Start, start);
    CHECK(fsm.timeLeftSeconds(start + 9999) == 1);
    fsm.update(start + 10000);
    CHECK(fsm.state() == ActuatorState::ARMED);
    CHECK(fsm.handle(Command::Deploy, start + 10000));
    fsm.update(start + 12999);
    CHECK(fsm.pulseActive());
    fsm.update(start + 13000);
    CHECK(!fsm.pulseActive());
  });
  test("watchdog survives millis rollover", [] {
    const uint32_t start = UINT32_MAX - 99;
    ActuatorFsm fsm(60000);
    fsm.completePost(true, start);
    fsm.handle(Command::Start, start);
    fsm.update(start + 30000);
    CHECK(fsm.error() == ErrorCode::ControlTimeout);
  });
  test("all state telemetry names match UART contract", [] {
    const ActuatorState states[] = {ActuatorState::POST, ActuatorState::SAFE,
      ActuatorState::ARMING, ActuatorState::ARMED, ActuatorState::ACTUATED,
      ActuatorState::FAULT};
    const char* names[] = {"POST", "SAFE", "ARMING", "ARMED", "ACTUATED", "FAULT"};
    for (std::size_t i = 0; i < 6; ++i)
      CHECK(std::strcmp(ActuatorFsm::stateName(states[i]), names[i]) == 0);
  });
  test("UART commands accept LF and CRLF", [] {
    UartLineParser parser;
    const char* lines[] = {"CMD:START\n", "CMD:STOP\r\n", "CMD:DEPLOY\n", "CMD:STATUS\n"};
    const Command commands[] = {Command::Start, Command::Stop, Command::Deploy, Command::Status};
    for (std::size_t i = 0; i < 4; ++i) {
      const auto result = feed(parser, lines[i]);
      CHECK(result.kind == ParseKind::CommandReady);
      CHECK(result.command == commands[i]);
    }
  });
  test("fragmented commands wait for newline; blanks are ignored", [] {
    UartLineParser parser;
    CHECK(feed(parser, "CMD:STA").kind == ParseKind::None);
    CHECK(feed(parser, "RT").kind == ParseKind::None);
    const auto result = feed(parser, "\n");
    CHECK(result.command == Command::Start);
    CHECK(feed(parser, "\n\r\n").kind == ParseKind::None);
  });
  test("malformed command is rejected without accepting prefixes", [] {
    UartLineParser parser;
    for (const char* line : {"CMD:DEPLOYx\n", "cmd:STOP\n", "CMD:START \n",
                             "CMD:ST\rOP\n", "HELLO\n"})
      CHECK(feed(parser, line).kind == ParseKind::Invalid);
    CHECK(feed(parser, std::string("CMD:STOP\0x\n", 11)).kind == ParseKind::Invalid);
    CHECK(feed(parser, "CMD:STATUS\n").command == Command::Status);
  });
  test("overflow reported once, suffix discarded, next line recovers", [] {
    UartLineParser parser;
    CHECK(feed(parser, std::string(64, 'X')).kind == ParseKind::Overflow);
    CHECK(feed(parser, "CMD:STOP\n").kind == ParseKind::None);
    CHECK(feed(parser, "CMD:STOP\n").command == Command::Stop);
  });
  test("exact telemetry format and truncation detection", [] {
    ActuatorFsm fsm(10000);
    fsm.completePost(true, 0);
    fsm.handle(Command::Start, 0);
    char line[96];
    CHECK(formatTelemetry(fsm, 1001, line, sizeof(line)));
    CHECK(std::strcmp(line, "STATE:ARMING,TIME_LEFT:9,ERR:0\n") == 0);
    CHECK(!formatTelemetry(fsm, 0, line, 4));
    fsm.fault(ErrorCode::UartOverflow, 2000);
    CHECK(formatTelemetry(fsm, 2000, line, sizeof(line)));
    CHECK(std::strcmp(line, "STATE:FAULT,TIME_LEFT:0,ERR:4\n") == 0);
  });
  test("SAFE 0.5Hz ARMING 2Hz ARMED 5Hz patterns", [] {
    struct Timing { ActuatorState state; uint32_t on; uint32_t period; };
    const Timing timings[] = {{ActuatorState::SAFE, 1000, 2000},
      {ActuatorState::ARMING, 250, 500}, {ActuatorState::ARMED, 100, 200}};
    for (const auto& timing : timings) {
      CHECK(indicatorOn(timing.state, 0));
      CHECK(indicatorOn(timing.state, timing.on - 1));
      CHECK(!indicatorOn(timing.state, timing.on));
      CHECK(!indicatorOn(timing.state, timing.period - 1));
      CHECK(indicatorOn(timing.state, timing.period));
    }
  });
  test("POST indication off", [] {
    CHECK(!indicatorOn(ActuatorState::POST, 0));
  });
  test("FAULT three short flashes and pause", [] {
    for (uint32_t time : {0U, 200U, 400U, 1600U})
      CHECK(indicatorOn(ActuatorState::FAULT, time));
    for (uint32_t time : {100U, 300U, 500U, 600U, 1599U})
      CHECK(!indicatorOn(ActuatorState::FAULT, time));
  });
  test("UART2 adapter initializes specified pins and serial framing", [] {
    ActuatorFsm fsm;
    HardwareSerial port;
    UartHandler uart(port, fsm, dispatchTestCommand);
    CHECK(uart.begin());
    CHECK(port.baudRate == 115200);
    CHECK(port.serialConfig == SERIAL_8N1);
    CHECK(port.rxPin == 16 && port.txPin == 17);
  });
  test("UART adapter replays early DEPLOY, arm, pulse and STOP", [] {
    ActuatorFsm fsm(10000);
    commandTarget = &fsm;
    fsm.completePost(true, 0);
    HardwareSerial port;
    UartHandler uart(port, fsm, dispatchTestCommand);
    uart.begin();
    port.input = "CMD:DEPLOY\nCMD:START\nCMD:DEPLOY\nCMD:STATUS\n";
    uart.update(0);
    CHECK(fsm.state() == ActuatorState::ARMING);
    CHECK(!fsm.pulseActive());
    CHECK(port.output.find("STATE:ARMING,TIME_LEFT:10,ERR:0\n") != std::string::npos);
    port.input = "CMD:DEPLOY\n";
    uart.update(10000);
    CHECK(fsm.state() == ActuatorState::ACTUATED);
    CHECK(fsm.pulseActive());
    port.input = "CMD:STOP\n";
    uart.update(10001);
    CHECK(fsm.state() == ActuatorState::SAFE);
    CHECK(!fsm.pulseActive());
  });
  test("UART periodic telemetry is 1Hz and does not count as heartbeat", [] {
    ActuatorFsm fsm(60000);
    commandTarget = &fsm;
    fsm.completePost(true, 0);
    fsm.handle(Command::Start, 0);
    HardwareSerial port;
    UartHandler uart(port, fsm, dispatchTestCommand);
    uart.begin();
    uart.update(999);
    CHECK(port.output.empty());
    uart.update(1000);
    CHECK(port.output == "STATE:ARMING,TIME_LEFT:59,ERR:0\n");
    const auto size = port.output.size();
    uart.update(1999);
    CHECK(port.output.size() == size);
    fsm.update(30000);
    CHECK(fsm.error() == ErrorCode::ControlTimeout);
  });
  test("UART oversized input faults and next STOP recovers", [] {
    ActuatorFsm fsm;
    commandTarget = &fsm;
    fsm.completePost(true, 0);
    HardwareSerial port;
    UartHandler uart(port, fsm, dispatchTestCommand);
    uart.begin();
    port.input = std::string(64, 'X') + "CMD:STOP\n";
    uart.update(1);
    CHECK(fsm.state() == ActuatorState::FAULT);
    CHECK(fsm.error() == ErrorCode::UartOverflow);
    port.input = "CMD:STOP\n";
    uart.update(2);
    CHECK(fsm.state() == ActuatorState::SAFE);
  });
  test("SoftAP starts with required credentials and serves panel", [] {
    ActuatorFsm fsm;
    commandTarget = &fsm;
    ActuatorWebServer web(fsm, dispatchTestWebCommand);
    CHECK(web.begin());
    CHECK(std::strcmp(WiFi.ssid, "ACTUATOR-SIM") == 0);
    CHECK(std::strcmp(WiFi.key, "password123") == 0);
    WebServer::latest->request("/", HTTP_GET);
    CHECK(WebServer::latest->statusCode == 200);
    CHECK(WebServer::latest->response.find("ACTUATOR-SIM") != std::string::npos);
  });
  test("SoftAP initialization failure is reported to POST", [] {
    ActuatorFsm fsm;
    WiFi.ready = false;
    ActuatorWebServer web(fsm, dispatchTestWebCommand);
    CHECK(!web.begin());
    WiFi.ready = true;
  });
  test("HTTP rejects early DEPLOY, supports arm/deploy/stop and status", [] {
    ActuatorFsm fsm(10000);
    commandTarget = &fsm;
    fsm.completePost(true, 0);
    fakeNow = 0;
    ActuatorWebServer web(fsm, dispatchTestWebCommand);
    CHECK(web.begin());
    auto& http = *WebServer::latest;
    http.request("/deploy", HTTP_POST);
    CHECK(http.statusCode == 409);
    CHECK(fsm.state() == ActuatorState::SAFE);
    http.request("/start", HTTP_POST);
    CHECK(http.statusCode == 200);
    CHECK(fsm.state() == ActuatorState::ARMING);
    http.request("/status", HTTP_GET);
    CHECK(http.response.find("\"state\":\"ARMING\"") != std::string::npos);
    CHECK(http.response.find("\"time_left\":10") != std::string::npos);
    http.request("/deploy", HTTP_POST);
    CHECK(http.statusCode == 409);
    fakeNow = 10000;
    http.request("/deploy", HTTP_POST);
    CHECK(http.statusCode == 200 && fsm.pulseActive());
    CHECK(http.response.find("\"pulse_active\":true") != std::string::npos);
    CHECK(http.response.find("\"deployment_count\":1") != std::string::npos);
    http.request("/stop", HTTP_POST);
    CHECK(http.statusCode == 200);
    CHECK(fsm.state() == ActuatorState::SAFE && !fsm.pulseActive());
    http.request("/missing", HTTP_GET);
    CHECK(http.statusCode == 404);
  });
  test("HTTP command GETs return 405 and never change state", [] {
    ActuatorFsm fsm;
    commandTarget = &fsm;
    fsm.completePost(true, 0);
    fakeNow = 0;
    ActuatorWebServer web(fsm, dispatchTestWebCommand);
    CHECK(web.begin());
    for (const char* path : {"/start", "/stop", "/deploy"}) {
      WebServer::latest->request(path, HTTP_GET);
      CHECK(WebServer::latest->statusCode == 405);
      CHECK(fsm.state() == ActuatorState::SAFE);
    }
  });
  test("HTTP command with unknown outcome returns 503, never a 409 rejection", [] {
    ActuatorFsm fsm;
    fsm.completePost(true, 0);
    ActuatorWebServer web(fsm, unavailableWebCommand);
    CHECK(web.begin());
    auto& http = *WebServer::latest;
    for (const char* path : {"/start", "/stop", "/deploy"}) {
      http.request(path, HTTP_POST);
      CHECK(http.statusCode == 503);
      CHECK(http.response.find("\"error\":\"command_outcome_unknown\"") != std::string::npos);
      CHECK(http.response.find("\"accepted\"") == std::string::npos);
    }
    http.request("/status", HTTP_GET);
    CHECK(http.statusCode == 200);
  });
  test("repeated web start registers routes and SoftAP only once", [] {
    ActuatorFsm fsm;
    commandTarget = &fsm;
    ActuatorWebServer web(fsm, dispatchTestWebCommand);
    const int softApCalls = WiFi.softApCalls;
    CHECK(web.begin());
    const int registrations = WebServer::latest->registrations;
    CHECK(web.begin());
    CHECK(WebServer::latest->registrations == registrations);
    CHECK(WiFi.softApCalls == softApCalls + 1);
  });
  test("web start retries SoftAP after an initial failure", [] {
    ActuatorFsm fsm;
    ActuatorWebServer web(fsm, dispatchTestWebCommand);
    WiFi.ready = false;
    CHECK(!web.begin());
    WiFi.ready = true;
    CHECK(web.begin());
    WebServer::latest->request("/", HTTP_GET);
    CHECK(WebServer::latest->statusCode == 200);
  });
  test("indication changes immediately on STOP and FAULT", [] {
    ActuatorFsm fsm(10);
    fsm.completePost(true, 0);
    Indicators indicators;
    fakePins[2] = HIGH;
    indicators.begin();
    CHECK(fakePins[2] == LOW);
    indicators.update(fsm, 0);
    CHECK(fakePins[2] == HIGH);
    indicators.update(fsm, 1000);
    CHECK(fakePins[2] == LOW);
    fsm.handle(Command::Start, 1000);
    fsm.handle(Command::Deploy, 1010);
    indicators.update(fsm, 1010);
    CHECK(fakePins[2] == HIGH);
    fsm.update(4010);
    indicators.update(fsm, 4010);
    CHECK(fakePins[2] == LOW);
    fsm.fault(ErrorCode::InvalidCommand, 4011);
    indicators.update(fsm, 4011);
    CHECK(fakePins[2] == HIGH);
  });
  test("LittleFS POST validates read/write and never autoformats", [] {
    LittleFS = LittleFSClass();
    LittleFS.files["/info.txt"] = "ACTUATOR-SIM\n";
    EventLog log;
    CHECK(log.begin());
    CHECK(!LittleFS.autoFormatted);
    CHECK(!LittleFS.exists("/.health"));
    LittleFS.mountWorks = false;
    CHECK(!log.begin());
    LittleFS.mountWorks = true;
    LittleFS.writesFail = true;
    CHECK(!log.begin());
    LittleFS = LittleFSClass();
    CHECK(!log.begin());
  });
  test("journal path failure remains unhealthy until that path is repaired", [] {
    LittleFS = LittleFSClass();
    LittleFS.files["/info.txt"] = "ACTUATOR-SIM\n";
    EventLog log;
    CHECK(log.begin());
    LittleFS.blockedPath = "/events.log";
    CHECK(!log.recordDeployment(1, 1));
    CHECK(!log.begin());
    LittleFS.blockedPath.clear();
    LittleFS.directoryPath = "/events.log";
    CHECK(!log.begin());
    LittleFS.directoryPath.clear();
    CHECK(log.begin());
  });
  test("POST and STOP health checks preserve both retained deployment journals", [] {
    LittleFS = LittleFSClass();
    LittleFS.files["/info.txt"] = "ACTUATOR-SIM\n";
    LittleFS.files["/events.log"] = std::string(4001, 'X');
    LittleFS.files["/events.previous.log"] = "older deployment records";
    const auto current = LittleFS.files["/events.log"];
    const auto previous = LittleFS.files["/events.previous.log"];
    EventLog log;
    CHECK(log.begin());
    CHECK(LittleFS.files["/events.log"] == current);
    CHECK(LittleFS.files["/events.previous.log"] == previous);
  });
  test("ACTUATED indication never restarts after a full millis cycle", [] {
    ActuatorFsm fsm(1);
    fsm.completePost(true, 0);
    fsm.handle(Command::Start, 0);
    fsm.handle(Command::Deploy, 1);
    Indicators indicators;
    indicators.begin();
    fsm.update(3001);
    indicators.update(fsm, 3001);
    CHECK(!fsm.pulseActive() && fakePins[2] == LOW);
    fsm.update(UINT32_MAX);
    fsm.update(1);
    indicators.update(fsm, 1);
    CHECK(!fsm.pulseActive() && fakePins[2] == LOW);
  });
  test("deployment journal appends persistent event and rotates bounded files", [] {
    LittleFS = LittleFSClass();
    LittleFS.files["/info.txt"] = "ACTUATOR-SIM\n";
    EventLog log;
    CHECK(log.begin());
    CHECK(log.recordDeployment(10000, 1));
    CHECK(LittleFS.files["/events.log"] == "UPTIME_MS:10000,STATE:ACTUATED,COUNT:1\n");
    const auto previous = std::string(config::kJournalMaxBytes - 10, 'X');
    LittleFS.files["/events.log"] = previous;
    CHECK(log.recordDeployment(20000, 2));
    CHECK(LittleFS.files["/events.previous.log"] == previous);
    CHECK(LittleFS.files["/events.log"] == "UPTIME_MS:20000,STATE:ACTUATED,COUNT:2\n");
    LittleFS.writesFail = true;
    CHECK(!log.recordDeployment(30000, 3));
  });
  test("complete runtime POST, storage-fault recovery and durable deployment", [] {
    LittleFS = LittleFSClass();
    LittleFS.files["/info.txt"] = "ACTUATOR-SIM\n";
    LittleFS.files["/settings.json"] = readText("data/settings.json");
    LittleFS.writesFail = true;
    WiFi.ready = true;
    fakeNow = 0;
    Serial.started = false;
    Serial2.started = false;
    setup();
    CHECK(Serial.started && Serial2.started);
    CHECK(Serial.output.find("POST:FS:FAIL") != std::string::npos);
    CHECK(Serial.output.find("POST:SETTINGS:OK") != std::string::npos);
    CHECK(Serial.output.find("DEVICE:MAC=24:6F:28:01:02:03,CHIP=ESP32-D0WD-V3,REV=3,FLASH_MB=4") !=
          std::string::npos);
    auto& http = *WebServer::latest;
    http.request("/status", HTTP_GET);
    CHECK(http.response.find("\"state\":\"FAULT\"") != std::string::npos);
    CHECK(http.response.find("\"err\":5") != std::string::npos);
    http.request("/stop", HTTP_POST);
    CHECK(http.statusCode == 409);
    LittleFS.writesFail = false;
    http.request("/stop", HTTP_POST);
    CHECK(http.statusCode == 200);
    Serial.input = "CMD:START\n";
    loop();
    for (fakeNow = 1000; fakeNow <= 300000; fakeNow += 1000) {
      http.request("/status", HTTP_GET);
      loop();
    }
    LittleFS.writesFail = true;
    http.request("/deploy", HTTP_POST);
    CHECK(http.statusCode == 409);
    CHECK(http.response.find("\"state\":\"FAULT\"") != std::string::npos);
    CHECK(http.response.find("\"pulse_active\":false") != std::string::npos);
    http.request("/stop", HTTP_POST);
    CHECK(http.statusCode == 409);
    LittleFS.writesFail = false;
    http.request("/stop", HTTP_POST);
    CHECK(http.statusCode == 200);
    http.request("/start", HTTP_POST);
    const auto start = fakeNow;
    for (fakeNow = start + 1000; fakeNow <= start + 300000; fakeNow += 1000) {
      http.request("/status", HTTP_GET);
      loop();
    }
    http.request("/deploy", HTTP_POST);
    CHECK(http.statusCode == 200);
    CHECK(http.response.find("\"pulse_active\":true") != std::string::npos);
    const auto recorded = LittleFS.files["/events.log"];
    CHECK(recorded.find("STATE:ACTUATED,COUNT:1\n") != std::string::npos);
    http.request("/deploy", HTTP_POST);
    CHECK(http.statusCode == 409);
    CHECK(LittleFS.files["/events.log"] == recorded);
    Serial.input = "CMD:STOP\n";
    loop();
    http.request("/status", HTTP_GET);
    CHECK(http.response.find("\"state\":\"SAFE\"") != std::string::npos);
    CHECK(http.response.find("\"pulse_active\":false") != std::string::npos);
  });
  test("PWM alone arms with heartbeat, deploys, stops and faults on signal loss", [] {
    auto& http = *WebServer::latest;
    runPwm(1000, 600);
    CHECK(Serial.output.find("PWM:1000us,BAND:STOP,CAPTURED:1,NEUTRAL:1") != std::string::npos);
    runPwm(1500, 400);
    CHECK(Serial.output.find("PWM:CMD:START") != std::string::npos);
    CHECK(lastTelemetry().find("STATE:ARMING") == 0);
    // Only PWM pulses for five minutes: the PWM heartbeat must keep arming alive.
    runPwm(1500, 300000);
    CHECK(lastTelemetry() == "STATE:ARMED,TIME_LEFT:0,ERR:0");
    runPwm(2000, 400);
    CHECK(lastTelemetry().find("STATE:ACTUATED") == 0);
    CHECK(LittleFS.files["/events.log"].find("STATE:ACTUATED,COUNT:2\n") != std::string::npos);
    runPwm(1000, 400);
    CHECK(lastTelemetry().find("STATE:SAFE") == 0);
    runPwm(1500, 400);
    CHECK(lastTelemetry().find("STATE:ARMING") == 0);
    runSilence(600);
    CHECK(Serial.output.find("PWM:LOST") != std::string::npos);
    CHECK(lastTelemetry() == "STATE:FAULT,TIME_LEFT:0,ERR:9");
    // A controller reappearing at STOP is a starting position, not a recovery request.
    runPwm(1000, 600);
    CHECK(lastTelemetry() == "STATE:FAULT,TIME_LEFT:0,ERR:9");
    http.request("/stop", HTTP_POST);
    CHECK(http.statusCode == 200);
    runSilence(600);
  });
  test("BOOT button press stops arming", [] {
    Serial.input = "CMD:START\n";
    runSilence(100);
    CHECK(lastTelemetry().find("STATE:ARMING") == 0);
    fakeInputLow[0] = true;
    runSilence(100);
    fakeInputLow[0] = false;
    runSilence(100);
    CHECK(Serial.output.find("BUTTON:STOP") != std::string::npos);
    CHECK(lastTelemetry().find("STATE:SAFE") == 0);
  });
  test("a simulated supply excursion faults and blocks recovery until restored", [] {
    auto& http = *WebServer::latest;
    Serial.input = "SIM:VIN:4000\n";
    runSilence(1100);
    CHECK(Serial.output.find("SIM:VIN:4000:OK") != std::string::npos);
    CHECK(lastTelemetry() == "STATE:FAULT,TIME_LEFT:0,ERR:10");
    http.request("/stop", HTTP_POST);
    CHECK(http.statusCode == 409);
    Serial.input = "SIM:VIN:5000\n";
    runSilence(100);
    http.request("/stop", HTTP_POST);
    CHECK(http.statusCode == 200);
    runSilence(100);
    CHECK(lastTelemetry().find("STATE:SAFE") == 0);
  });
  runFsmHeartbeatTests();
  runSettingsTests();
  runPwmDecoderTests();
  runPowerMonitorTests();
  runButtonMonitorTests();
  runSelfTestTests();
  runSimVinTests();
  std::cout << cases - failures << '/' << cases << " cases passed\n";
  return failures == 0 ? 0 : 1;
}
