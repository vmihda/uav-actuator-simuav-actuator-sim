#include "pwm_input.h"

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>

namespace {
portMUX_TYPE pulseMux = portMUX_INITIALIZER_UNLOCKED;
gpio_num_t inputPin = GPIO_NUM_NC;
int64_t risingAt = 0;  // Written only by the ISR.
uint16_t latestWidth = 0;
uint32_t latestSeq = 0;
uint32_t takenSeq = 0;

// Measures the high time of each pulse; the main loop reads only the latest.
void IRAM_ATTR onEdge(void*) {
  const int64_t now = esp_timer_get_time();
  if (gpio_get_level(inputPin)) {
    risingAt = now;
    return;
  }
  if (risingAt == 0) return;
  const int64_t width = now - risingAt;
  if (width <= 0) return;
  portENTER_CRITICAL_ISR(&pulseMux);
  latestWidth = width > 65535 ? 65535 : static_cast<uint16_t>(width);
  ++latestSeq;
  portEXIT_CRITICAL_ISR(&pulseMux);
}
}  // namespace

bool pwmInputBegin(int pin) {
  inputPin = static_cast<gpio_num_t>(pin);
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << pin;
  io.mode = GPIO_MODE_INPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  // A disconnected wire reads low, which the decoder reports as signal loss.
  io.pull_down_en = GPIO_PULLDOWN_ENABLE;
  io.intr_type = GPIO_INTR_ANYEDGE;
  if (gpio_config(&io) != ESP_OK) return false;
  const esp_err_t service = gpio_install_isr_service(0);
  if (service != ESP_OK && service != ESP_ERR_INVALID_STATE) return false;
  return gpio_isr_handler_add(inputPin, onEdge, nullptr) == ESP_OK;
}

bool pwmInputTake(uint16_t& widthUs) {
  portENTER_CRITICAL(&pulseMux);
  const bool fresh = latestSeq != takenSeq;
  if (fresh) {
    widthUs = latestWidth;
    takenSeq = latestSeq;
  }
  portEXIT_CRITICAL(&pulseMux);
  return fresh;
}
