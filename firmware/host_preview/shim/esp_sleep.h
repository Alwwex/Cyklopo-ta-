#pragma once
#include <cstdlib>
#define SOC_GPIO_SUPPORT_DEEPSLEEP_WAKEUP 1
#define ESP_GPIO_WAKEUP_GPIO_LOW 0
inline int esp_deep_sleep_enable_gpio_wakeup(uint64_t, int) { return 0; }
inline void esp_deep_sleep_start() { printf("[preview] deep sleep\n"); exit(0); }
