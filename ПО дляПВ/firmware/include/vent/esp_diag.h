#pragma once
// Только для прошивки: причина последнего сброса ESP32 (packages/core/diagnostics.yaml).

#ifdef USE_ESP32
#include <cstdint>
#include <esp_system.h>

namespace vent {
namespace diag {

enum class ResetKind : uint8_t { NORMAL, WATCHDOG, BROWNOUT };

inline ResetKind reset_kind() {
  switch (esp_reset_reason()) {
    case ESP_RST_BROWNOUT: return ResetKind::BROWNOUT;
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT: return ResetKind::WATCHDOG;
    default: return ResetKind::NORMAL;
  }
}

inline const char *reset_reason_text() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "включение питания";
    case ESP_RST_EXT: return "внешний сброс";
    case ESP_RST_SW: return "программная перезагрузка (кнопка, OTA)";
    case ESP_RST_PANIC: return "сбой программы (panic)";
    case ESP_RST_INT_WDT: return "сторожевой таймер прерываний";
    case ESP_RST_TASK_WDT: return "сторожевой таймер задач (зависание)";
    case ESP_RST_WDT: return "сторожевой таймер";
    case ESP_RST_DEEPSLEEP: return "выход из сна";
    case ESP_RST_BROWNOUT: return "просадка питания (brownout)";
    case ESP_RST_SDIO: return "сброс по SDIO";
    default: return "неизвестно";
  }
}

}  // namespace diag
}  // namespace vent
#endif  // USE_ESP32
