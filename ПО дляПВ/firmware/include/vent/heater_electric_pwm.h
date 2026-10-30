#pragma once
// Электрический ТЭН через SSR с медленным ШИМ (slow_pwm), ПИ-регулятор по t_supply.
// Защиты драйвера:
//  - вход аварийного термостата +100 °C (ручной сброс) → авария со стопом;
//  - вход термостата +50 °C (опц.) → запрет ТЭНа, пока разомкнут;
//  - «не хватает мощности»: выход на максимуме долго, уставка не достигается;
//  - «нагрев не управляется»: команда 0, а приток горячее улицы и помещения (пробит SSR).

#include "alarms.h"
#include "control_math.h"
#include "heater.h"

namespace vent {

struct ElectricPwmConfig {
  float out_max{100.0f};               // %
  float full_pct{98.0f};
  uint32_t insufficient_ms{900000};
  float insufficient_band{2.0f};       // °C ниже уставки
  float uncontrolled_margin_c{8.0f};
  uint32_t uncontrolled_ms{180000};
};

class ElectricPwmHeater : public HeaterDriver {
 public:
  // ssr — выход slow_pwm; kp/ti — настраиваемые на лету параметры ПИ;
  // th100 — аварийный термостат (nullptr — не подключён); th50 — опционально.
  ElectricPwmHeater(AnalogOut *ssr, AnalogIn *kp, AnalogIn *ti, SignalIn *th100, SignalIn *th50,
                    const ElectricPwmConfig &cfg)
      : ssr_(ssr), kp_(kp), ti_(ti), th100_(th100), th50_(th50), cfg_(cfg) {}

  const char *type_name() const override { return "Электро, SSR с ШИМ"; }
  bool needs_airflow() const override { return true; }

  void update(const HeatContext &c, AlarmSink &alarms) override {
    const bool th100_open = th100_ && !th100_->ok();
    const bool th50_open = th50_ && !th50_->ok();
    alarms.set(AL_THERMOSTAT, th100_open);
    alarms.set(AL_THERMOSTAT50, th50_open);

    const bool on = c.heat_allowed && c.airflow_ok && !th100_open && !th50_open &&
                    !std::isnan(c.t_supply) && !std::isnan(c.setpoint);
    if (on) {
      out_ = pi_.update(c.setpoint, c.t_supply, kp_->value(), ti_->value(), cfg_.out_max, c.dt_s);
    } else {
      pi_.reset();
      out_ = 0.0f;
    }
    ssr_->set(out_ / 100.0f);

    const bool at_max = on && out_ >= cfg_.full_pct && c.t_supply < c.setpoint - cfg_.insufficient_band;
    if (!at_max)
      max_since_ = c.now_ms;
    alarms.set(AL_HEAT_INSUFFICIENT, c.now_ms - max_since_ >= cfg_.insufficient_ms);

    // Тепло без команды: приток работает, ТЭН и преднагрев выключены, а воздух горячее
    // и улицы, и помещения.
    float t_ref = NAN;
    if (!std::isnan(c.t_outdoor)) t_ref = c.t_outdoor;
    if (!std::isnan(c.t_extract) && (std::isnan(t_ref) || c.t_extract > t_ref)) t_ref = c.t_extract;
    const bool uncontrolled = out_ == 0.0f && !c.preheat_active && c.supply_running_ms >= 60000 &&
                              !std::isnan(c.t_supply) && !std::isnan(t_ref) &&
                              c.t_supply > t_ref + cfg_.uncontrolled_margin_c;
    if (!uncontrolled)
      uncontrolled_since_ = c.now_ms;
    alarms.set(AL_HEAT_UNCONTROLLED, c.now_ms - uncontrolled_since_ >= cfg_.uncontrolled_ms);
  }

  float output_pct() const override { return out_; }
  const PiController &pi() const { return pi_; }

 private:
  AnalogOut *ssr_;
  AnalogIn *kp_, *ti_;
  SignalIn *th100_, *th50_;
  ElectricPwmConfig cfg_;
  PiController pi_{};
  float out_{0.0f};
  uint32_t max_since_{0};
  uint32_t uncontrolled_since_{0};
};

}  // namespace vent
