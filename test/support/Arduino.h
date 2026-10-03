#pragma once

#include <cstdint>
#include <string>
#include <cstdio>

#define PROGMEM
constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int OUTPUT = 1;
constexpr int INPUT = 0;
constexpr int INPUT_PULLUP = 5;
extern uint32_t fakeNow;
extern int fakePins[40];
extern bool fakeInputLow[40];
inline uint32_t millis() { return fakeNow; }
inline void delay(uint32_t ms) { fakeNow += ms; }
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int value) { fakePins[pin] = value; }
inline int digitalRead(int pin) { return fakeInputLow[pin] ? LOW : HIGH; }

constexpr uint32_t SERIAL_8N1 = 0x800001c;

class HardwareSerial {
 public:
  void begin(uint32_t baud, uint32_t framing, int rx, int tx) {
    baudRate = baud; serialConfig = framing; rxPin = rx; txPin = tx; started = true;
  }
  std::size_t setRxBufferSize(std::size_t size) { return size; }
  explicit operator bool() const { return started; }
  int available() const { return static_cast<int>(input.size()); }
  int availableForWrite() const { return 256; }
  int read() {
    if (input.empty()) return -1;
    const unsigned char byte = static_cast<unsigned char>(input[0]);
    input.erase(0, 1);
    return byte;
  }
  std::size_t write(const uint8_t* bytes, std::size_t size) {
    output.append(reinterpret_cast<const char*>(bytes), size);
    return size;
  }
  void begin(uint32_t baud) { baudRate = baud; started = true; }
  template <typename... Args>
  void printf(const char* format, Args... args) {
    char buffer[256]; std::snprintf(buffer, sizeof(buffer), format, args...); output += buffer;
  }
  void println(const char* text) { output += text; output += '\n'; }
  uint32_t baudRate = 0;
  uint32_t serialConfig = 0;
  int rxPin = -1;
  int txPin = -1;
  bool started = false;
  std::string input;
  std::string output;
};
extern HardwareSerial Serial;
extern HardwareSerial Serial2;
inline void enableLoopWDT() {}
inline void yield() {}
