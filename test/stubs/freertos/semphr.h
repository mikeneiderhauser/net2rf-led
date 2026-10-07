#pragma once
#include <cstdint>
typedef void *SemaphoreHandle_t;
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) (ms)
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex() { return (void *) 1; }
inline int xSemaphoreTakeRecursive(SemaphoreHandle_t, uint32_t) { return 1; }
inline int xSemaphoreGiveRecursive(SemaphoreHandle_t) { return 1; }
inline int xTaskCreatePinnedToCore(void (*)(void *), const char *, uint32_t, void *, int, void *, int) { return 1; }
inline void vTaskDelay(uint32_t) {}
