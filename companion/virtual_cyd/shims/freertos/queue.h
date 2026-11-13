#pragma once

#include "FreeRTOS.h"
#include "../../virtual_hw.h"
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

struct VcydQueue {
    size_t capacity = 8;
    size_t itemSize = 1;
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> items;
};

inline QueueHandle_t xQueueCreate(uint32_t uxQueueLength, uint32_t uxItemSize) {
    auto* q = new VcydQueue();
    q->capacity = uxQueueLength ? uxQueueLength : 1;
    q->itemSize = uxItemSize ? uxItemSize : 1;
    return q;
}

inline BaseType_t xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue, TickType_t xTicksToWait) {
    (void)xTicksToWait;
    if (!xQueue || !pvItemToQueue) return pdFALSE;
    auto* q = static_cast<VcydQueue*>(xQueue);
    std::lock_guard<std::mutex> lk(q->mtx);
    if (q->items.size() >= q->capacity) return pdFALSE;
    std::vector<uint8_t> buf(q->itemSize);
    std::memcpy(buf.data(), pvItemToQueue, q->itemSize);
    q->items.push_back(std::move(buf));
    q->cv.notify_one();
    return pdTRUE;
}

inline void xQueueReset(QueueHandle_t xQueue) {
    if (!xQueue) return;
    auto* q = static_cast<VcydQueue*>(xQueue);
    std::lock_guard<std::mutex> lk(q->mtx);
    q->items.clear();
}

inline BaseType_t xQueueReceive(QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait) {
    if (!xQueue || !pvBuffer) return pdFALSE;
    auto* q = static_cast<VcydQueue*>(xQueue);
    std::unique_lock<std::mutex> lk(q->mtx);
    auto has_item = [&] {
        return !q->items.empty() || vcyd::VirtualHw::instance().shutdownRequested();
    };
    if (xTicksToWait == portMAX_DELAY) {
        while (!has_item()) {
            q->cv.wait_for(lk, std::chrono::milliseconds(50), has_item);
        }
    } else if (xTicksToWait > 0) {
        q->cv.wait_for(lk, std::chrono::milliseconds(xTicksToWait), has_item);
    }
    if (vcyd::VirtualHw::instance().shutdownRequested()) return pdFALSE;
    if (q->items.empty()) return pdFALSE;
    std::memcpy(pvBuffer, q->items.front().data(), q->itemSize);
    q->items.pop_front();
    return pdTRUE;
}
