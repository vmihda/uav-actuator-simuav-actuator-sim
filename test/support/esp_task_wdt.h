#pragma once

constexpr int ESP_OK = 0;
inline int esp_task_wdt_init(int, bool) { return ESP_OK; }
inline int esp_task_wdt_status(void*) { return ESP_OK; }
