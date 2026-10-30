#pragma once
// Нагрев: ПИ-регулятор температуры притока (выход 0…100 % на slow_pwm SSR ТЭНа)
// и гистерезис преднагревателя.

#include <cmath>

namespace vent {

inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

// ПИ с anti-windup:
//  - интегратор ограничен 0…out_max;
//  - интегрирование останавливается, когда выход уже в насыщении в сторону ошибки
//    (условное интегрирование). При 1,5 кВт на 400 м³/ч ТЭН поднимает приток
//    максимум на ≈ 11 °C: в мороз выход часто стоит на 100 %, и без этого интегратор
//    «накопил» бы лишнее и дал перерегулирование, когда уставка станет достижимой.
class PiController {
 public:
  void reset() {
    integral_ = 0.0f;
    output_ = 0.0f;
  }

  // kp — %/°C, ti_s — с (0 = без И-составляющей), out_max — %, dt_s — шаг, с.
  float update(float setpoint, float measured, float kp, float ti_s, float out_max, float dt_s) {
    if (std::isnan(setpoint) || std::isnan(measured) || dt_s <= 0.0f) {
      reset();
      return 0.0f;
    }
    const float error = setpoint - measured;
    const float p = kp * error;
    float candidate = integral_;
    if (ti_s > 0.0f)
      candidate += kp * error * dt_s / ti_s;
    const float unclamped = p + candidate;
    const bool saturated_high = unclamped > out_max && error > 0.0f;
    const bool saturated_low = unclamped < 0.0f && error < 0.0f;
    if (!saturated_high && !saturated_low)
      integral_ = candidate;
    integral_ = clampf(integral_, 0.0f, out_max);
    output_ = clampf(p + integral_, 0.0f, out_max);
    return output_;
  }

  float output() const { return output_; }
  float integral() const { return integral_; }

 private:
  float integral_{0.0f};
  float output_{0.0f};
};

// Преднагреватель: вкл ниже on_below, выкл выше off_above.
class Hysteresis {
 public:
  bool update(float value, float on_below, float off_above) {
    if (std::isnan(value)) {
      state_ = false;
    } else if (value < on_below) {
      state_ = true;
    } else if (value > off_above) {
      state_ = false;
    }
    return state_;
  }
  void reset() { state_ = false; }
  bool state() const { return state_; }

 private:
  bool state_{false};
};

}  // namespace vent
