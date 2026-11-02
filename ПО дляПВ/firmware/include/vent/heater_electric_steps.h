#pragma once
// Электронагрев ступенями на контакторах + плавная ступень 0–10 В (тиристорный регулятор
// или 3~ SSR с аналоговым входом). Для трёхфазных ТЭНов установок до 2000 м³/ч.
//
// ПИ по t_supply даёт требуемую мощность 0…100 % от полной. Полная мощность = steps ступеней
// + плавная ступень мощностью analog_units ступени (0 — плавной нет, только контакторы).
// Плавная ступень добирает остаток; когда её не хватает (или её «слишком много») — включается
// (выключается) следующий контактор, с гистерезисом hyst_units и не чаще min_switch_ms
// (ресурс контакторов). Запрет нагрева (нет потока, аварии, термостаты) — всё выключается
// сразу, без выдержки.
// Защиты — общие для электронагрева (heater_electric_common.h).

#include "control_math.h"
#include "heater_electric_common.h"

namespace vent {

struct ElectricStepsConfig : ElectricHeaterConfig {
  float analog_units{1.0f};     // мощность плавной ступени в долях ступени (0 — нет плавной)
  float hyst_units{0.15f};      // гистерезис переключения контакторов, в долях ступени
  uint32_t min_switch_ms{30000};
};

class ElectricStepsHeater : public HeaterDriver {
 public:
  static const uint8_t MAX_STEPS = 6;

  // steps[0] — первая ступень … steps[n-1]; analog — плавная ступень (nullptr — нет).
  ElectricStepsHeater(DigitalOut *const *steps, uint8_t n, AnalogOut *analog, AnalogIn *kp, AnalogIn *ti,
                      SignalIn *th100, SignalIn *th50, const ElectricStepsConfig &cfg)
      : analog_(analog), kp_(kp), ti_(ti), guard_(th100, th50, cfg), cfg_(cfg) {
    n_ = n > MAX_STEPS ? MAX_STEPS : n;
    for (uint8_t i = 0; i < n_; i++)
      steps_[i] = steps[i];
    if (!analog_)
      cfg_.analog_units = 0.0f;
  }

  const char *type_name() const override { return "Электро, ступени контакторами + плавная 0–10 В"; }
  bool needs_airflow() const override { return true; }

  void update(const HeatContext &c, AlarmSink &alarms) override {
    const bool on = guard_.permit(c, alarms);
    const float a = cfg_.analog_units;
    const float units = n_ + a;
    float analog = 0.0f;
    if (!on || units <= 0.0f) {
      pi_.reset();
      on_ = 0;  // запрет — сразу, без выдержки
    } else {
      const float demand = pi_.update(c.setpoint, c.t_supply, kp_->value(), ti_->value(), guard_.config().out_max, c.dt_s);
      const float want = demand / 100.0f * units;  // в ступенях
      const float h = cfg_.hyst_units;
      int target = on_;
      if (a > 0.0f) {
        if (want > on_ + a + h) target = on_ + 1;
        else if (want < on_ - h) target = on_ - 1;
      } else {
        if (want > on_ + 0.5f + h) target = on_ + 1;
        else if (want < on_ - 0.5f - h) target = on_ - 1;
      }
      target = target < 0 ? 0 : (target > n_ ? n_ : target);
      if (target != on_ && (!switched_ || c.now_ms - last_switch_ >= cfg_.min_switch_ms)) {
        on_ = (uint8_t) target;
        last_switch_ = c.now_ms;
        switched_ = true;
      }
      if (a > 0.0f)
        analog = clampf((want - on_) / a, 0.0f, 1.0f);
    }
    for (uint8_t i = 0; i < n_; i++)
      steps_[i]->set(i < on_);
    if (analog_)
      analog_->set(analog);
    out_ = units > 0.0f ? (on_ + analog * a) / units * 100.0f : 0.0f;
    guard_.check(c, on, out_, alarms);
  }

  float output_pct() const override { return out_; }
  uint8_t steps_on() const { return on_; }
  const PiController &pi() const { return pi_; }

 private:
  DigitalOut *steps_[MAX_STEPS]{};
  uint8_t n_{0};
  AnalogOut *analog_;
  AnalogIn *kp_, *ti_;
  ElectricHeaterGuard guard_;
  ElectricStepsConfig cfg_;
  PiController pi_{};
  uint8_t on_{0};
  bool switched_{false};
  uint32_t last_switch_{0};
  float out_{0.0f};
};

}  // namespace vent
