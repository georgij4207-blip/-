// Хост-тест функций firmware/include/vent_sensors.h.
// Сборка: g++ -std=c++17 -I../firmware/include test_vent_sensors.cpp -o t && ./t
#include <cassert>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include "vent_sensors.h"

using namespace vent;

static float ntc_r(float t_c) {  // NTC 10k, B=3380 — как в прошивке
  return 10000.0f * std::exp(3380.0f * (1.0f / (t_c + 273.15f) - 1.0f / 298.15f));
}
static float ntc_t(float r) {
  return 1.0f / (1.0f / 298.15f + std::log(r / 10000.0f) / 3380.0f) - 273.15f;
}
static float node_v(float r_ntc, float r_top, float vref) { return vref * r_ntc / (r_top + r_ntc); }

int main() {
  const float VMIN = 3.13f, VMAX = 3.47f;
  // ADS1115: 22 кОм, ратиометрия при разном опорном напряжении
  for (float vref : {3.20f, 3.30f, 3.40f}) {
    for (float t : {-40.0f, -20.0f, 0.0f, 25.0f, 45.0f, 60.0f}) {
      float v = node_v(ntc_r(t), 22000.0f, vref);
      assert(classify_divider(v, vref, 0.970f, 0.045f, VMIN, VMAX) == SENSOR_OK);
      float tr = ntc_t(divider_low_side_resistance(v, vref, 22000.0f));
      assert(std::fabs(tr - t) < 0.05f);
    }
    assert(classify_divider(vref, vref, 0.970f, 0.045f, VMIN, VMAX) == SENSOR_OPEN);
    assert(classify_divider(0.0f, vref, 0.970f, 0.045f, VMIN, VMAX) == SENSOR_SHORT);
  }
  assert(classify_divider(NAN, 3.3f, 0.970f, 0.045f, VMIN, VMAX) == SENSOR_NO_DATA);
  assert(classify_divider(1.0f, NAN, 0.970f, 0.045f, VMIN, VMAX) == SENSOR_REF_FAULT);
  assert(classify_divider(1.0f, 2.9f, 0.970f, 0.045f, VMIN, VMAX) == SENSOR_REF_FAULT);
  assert(final_status(SENSOR_OK, NAN) == SENSOR_OUT_OF_RANGE);
  assert(final_status(SENSOR_OK, 20.0f) == SENSOR_OK);
  // Порт ESP32: 4,7 кОм, рабочий диапазон притока
  for (float t : {0.0f, 18.0f, 45.0f, 60.0f}) {
    float v = node_v(ntc_r(t), 4700.0f, 3.30f);
    assert(classify_divider(v, 3.30f, 0.920f, 0.045f, VMIN, VMAX) == SENSOR_OK);
  }
  assert(classify_divider(3.30f, 3.30f, 0.920f, 0.045f, VMIN, VMAX) == SENSOR_OPEN);
  // Пороги в градусах
  float r_open = 22000.0f * 0.970f / 0.030f, r_short = 22000.0f * 0.045f / 0.955f;
  std::printf("ADS: обрыв ниже %.1f °C, КЗ выше %.1f °C\n", ntc_t(r_open), ntc_t(r_short));
  std::printf("ESP32: обрыв ниже %.1f °C\n", ntc_t(4700.0f * 0.92f / 0.08f));
  std::printf("OK\n");
  return 0;
}
