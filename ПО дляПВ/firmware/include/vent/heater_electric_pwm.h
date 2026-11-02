#pragma once
// Электрический ТЭН через SSR с медленным ШИМ (slow_pwm), ПИ-регулятор по t_supply.
// Защиты — общие для электронагрева (heater_electric_common.h).

#include "control_math.h"
#include "heater_electric_common.h"

namespace vent {

using ElectricPwmConfig = ElectricHeaterConfig;

class ElectricPwmHeater : public HeaterDriver {
 public:
  // ssr — выход slow_pwm; kp/ti — настраиваемые на лету параметры ПИ;
  // th100 — аварийный термостат (nullptr — не подключён); th50 — опционально.
  ElectricPwmHeater(AnalogOut *ssr, AnalogIn *kp, AnalogIn *ti, SignalIn *th100, SignalIn *th50,
                    const ElectricPwmConfig &cfg)
      : ssr_(ssr), kp_(kp), ti_(ti), guard_(th100, th50, cfg) {}

  const char *type_name() const override { return "Электро, SSR с ШИМ"; }
  bool needs_airflow() const override { return true; }

  void update(const HeatContext &c, AlarmSink &alarms) override {
    const bool on = guard_.permit(c, alarms);
    if (on) {
      out_ = pi_.update(c.setpoint, c.t_supply, kp_->value(), ti_->value(), guard_.config().out_max, c.dt_s);
    } else {
      pi_.reset();
      out_ = 0.0f;
    }
    ssr_->set(out_ / 100.0f);
    guard_.check(c, on, out_, alarms);
  }

  float output_pct() const override { return out_; }
  const PiController &pi() const { return pi_; }

 private:
  AnalogOut *ssr_;
  AnalogIn *kp_, *ti_;
  ElectricHeaterGuard guard_;
  PiController pi_{};
  float out_{0.0f};
};

}  // namespace vent
