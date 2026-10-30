#pragma once
// Диагностика и пересчёт резистивных датчиков в делителе:
//   Uref ── R_top ── узел (измеряется) ── датчик ── GND
// Функции чистые (без ESPHome), чтобы их можно было проверить в симуляции.

#include <cmath>
#include <cstdint>

namespace vent {

enum SensorStatus : uint8_t {
  SENSOR_OK = 0,
  SENSOR_NO_DATA = 1,       // нет свежих данных (АЦП не отвечает, I2C)
  SENSOR_OPEN = 2,          // обрыв датчика
  SENSOR_SHORT = 3,         // короткое замыкание датчика
  SENSOR_OUT_OF_RANGE = 4,  // температура вне допустимого диапазона
  SENSOR_REF_FAULT = 5,     // опорное напряжение делителя вне нормы
};

inline const char *sensor_status_text(SensorStatus s) {
  switch (s) {
    case SENSOR_OK: return "норма";
    case SENSOR_NO_DATA: return "нет данных";
    case SENSOR_OPEN: return "обрыв";
    case SENSOR_SHORT: return "КЗ";
    case SENSOR_OUT_OF_RANGE: return "вне диапазона";
    case SENSOR_REF_FAULT: return "ошибка опорного 3,3 В";
    default: return "неизвестно";
  }
}

// Классификация по отношению U/Uref: не зависит от точного значения Uref.
inline SensorStatus classify_divider(float v, float vref, float open_ratio, float short_ratio,
                                     float vref_min, float vref_max) {
  if (std::isnan(vref) || vref < vref_min || vref > vref_max)
    return SENSOR_REF_FAULT;
  if (std::isnan(v))
    return SENSOR_NO_DATA;
  const float ratio = v / vref;
  if (ratio >= open_ratio)
    return SENSOR_OPEN;
  if (ratio <= short_ratio)
    return SENSOR_SHORT;
  return SENSOR_OK;
}

// Сопротивление нижнего плеча делителя; NAN, если пересчёт невозможен.
inline float divider_low_side_resistance(float v, float vref, float r_top) {
  if (std::isnan(v) || std::isnan(vref) || !(vref > v) || v < 0.0f)
    return NAN;
  return r_top * v / (vref - v);
}

// Итоговый статус: делитель в норме, но температура не получена/вне диапазона.
inline SensorStatus final_status(SensorStatus divider, float temperature) {
  if (divider == SENSOR_OK && std::isnan(temperature))
    return SENSOR_OUT_OF_RANGE;
  return divider;
}

}  // namespace vent
