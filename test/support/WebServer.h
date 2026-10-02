#pragma once

#include <functional>
#include <map>
#include <string>

enum HTTPMethod { HTTP_ANY, HTTP_GET, HTTP_POST };

class WebServer {
 public:
  explicit WebServer(int) { latest = this; }
  void on(const char* path, HTTPMethod method, std::function<void()> handler) {
    routes[{path, method}] = handler;
    ++registrations;
  }
  void onNotFound(std::function<void()> handler) { notFound = handler; }
  void begin() { started = true; latest = this; }
  void handleClient() {}
  void sendHeader(const char*, const char*) {}
  void send(int code, const char*, const char* body) { statusCode = code; response = body; }
  void send_P(int code, const char* type, const char* body) { send(code, type, body); }
  const std::string& uri() const { return path_; }
  HTTPMethod method() const { return method_; }
  void request(const char* path, HTTPMethod method) {
    path_ = path; method_ = method; response.clear(); statusCode = 0;
    const auto it = routes.find({path, method});
    if (it != routes.end()) it->second();
    else if (notFound) notFound();
  }
  static WebServer* latest;
  bool started = false;
  int statusCode = 0;
  int registrations = 0;
  std::string response;
 private:
  std::map<std::pair<std::string, HTTPMethod>, std::function<void()>> routes;
  std::function<void()> notFound;
  std::string path_;
  HTTPMethod method_ = HTTP_GET;
};
