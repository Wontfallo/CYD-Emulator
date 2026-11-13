#pragma once

#include "FreeRTOS.h"
#include "../../virtual_hw.h"
#include <memory>
#include <thread>

inline BaseType_t xTaskCreatePinnedToCore(
    TaskFunction_t pvTaskCode,
    const char* pcName,
    uint32_t usStackDepth,
    void* pvParameters,
    uint32_t uxPriority,
    TaskHandle_t* pxCreatedTask,
    BaseType_t xCoreID)
{
    (void)pcName; (void)usStackDepth; (void)uxPriority; (void)xCoreID;
    auto th = std::make_shared<std::thread>([pvTaskCode, pvParameters]() {
        pvTaskCode(pvParameters);
    });
    vcyd::VirtualHw::instance().registerWorker([th]() {
        if (th->joinable()) th->join();
    });
    if (pxCreatedTask) *pxCreatedTask = reinterpret_cast<TaskHandle_t>(1);
    return pdTRUE;
}

inline void vTaskDelay(TickType_t ticks) {
    vcyd::VirtualHw::instance().delayMs(static_cast<uint32_t>(ticks));
}
