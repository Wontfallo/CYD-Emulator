#pragma once

#include <cstdint>

using BaseType_t = int32_t;
using TickType_t = uint32_t;

#define pdFALSE 0
#define pdTRUE  1
#define portMAX_DELAY 0xFFFFFFFFu
#define pdMS_TO_TICKS(ms) static_cast<TickType_t>(ms)

using QueueHandle_t = void*;
using TaskHandle_t  = void*;
using TaskFunction_t = void(*)(void*);

