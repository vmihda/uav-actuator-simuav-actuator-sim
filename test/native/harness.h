#pragma once

#include <iostream>
#include <stdexcept>
#include <string>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error( \
  std::string(__FILE__) + ":" + std::to_string(__LINE__) + ": " #condition); } while (false)

extern int failures;
extern int cases;

template <typename Test>
void test(const char* name, Test body) {
  ++cases;
  try { body(); std::cout << "PASS " << name << '\n'; }
  catch (const std::exception& error) {
    ++failures;
    std::cerr << "FAIL " << name << ": " << error.what() << '\n';
  }
}
