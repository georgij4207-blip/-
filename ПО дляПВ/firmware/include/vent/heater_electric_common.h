#pragma once
// Общие защиты электронагревателя (heater_electric_pwm.h, heater_electric_steps.h):
//  - вход аварийного термостата +100 °C (ручной сброс) → авария со стопом;
//  - вход термостата +50 °C (опц.) → запрет нагрева, пока разомкнут;
//  - «не хватает мощности»: выход на максимуме долго, уставка не достигается;
//  - «нагрев не управляется»: команда 0, а приток горячее улицы и помещения (пробит SSR,
//    залип контактор).

#include <cmath>

#include "alarms.h"
#include "heater.h"

namespace vent {

struct ElectricHeaterConfig {
  float out_max{100.0f};               // %
  float full_pct{98.0f};
  uint32_t insufficient_ms{900000};
  float insufficient_band{2.0f};       // °C ниже уставки
  float uncontrolled_margin_c{8.0f};
  uint32_t uncontrolled_ms{180000};
};

class ElectricHeaterGuard {
 public:
  ElectricHeaterGuard(SignalIn *th100, SignalIn *th50, const ElectricHeaterConfig &cfg)
      : th100_(th100), th50_(th50), cfg_(cfg) {}

  // До регулирования: термостаты и условия системы. true — греть можно.
  bool permit(const HeatContext &c, AlarmSink &alarms) {
    const bool th100_open = th100_ && !th100_->ok();
    const bool th50_open = th50_ && !th50_->ok();
    alarms.set(AL_THERMOSTAT, th100_open);
    alarms.set(AL_THERMOSTAT50, th50_open);
    return c.heat_allowed && c.airflow_ok && !th100_open && !th50_open && !std::isnan(c.t_supply) &&
           !std::isnan(c.setpoint);
  }

  // После регулирования: out_pct — реально поданная мощность, %.
  void check(const HeatContext &c, bool on, float out_pct, AlarmSink &alarms) {
    const bool at_max = on && out_pct >= cfg_.full_pct && c.t_supply < c.setpoint - cfg_.insufficient_band;
    if (!at_max)
      max_since_ = c.now_ms;
    alarms.set(AL_HEAT_INSUFFICIENT, c.now_ms - max_since_ >= cfg_.insufficient_ms);

    // Тепло без команды: приток работает, нагрев и преднагрев выключены, а воздух горячее
    // и улицы, и помещения.
    float t_ref = NAN;
    if (!std::isnan(c.t_outdoor)) t_ref = c.t_outdoor;
    if (!std::isnan(c.t_extract) && (std::isnan(t_ref) || c.t_extract > t_ref)) t_ref = c.t_extract;
    const bool uncontrolled = out_pct == 0.0f && !c.preheat_active && c.supply_running_ms >= 60000 &&
                              !std::isnan(c.t_supply) && !std::isnan(t_ref) &&
                              c.t_supply > t_ref + cfg_.uncontrolled_margin_c;
    if (!uncontrolled)
      uncontrolled_since_ = c.now_ms;
    alarms.set(AL_HEAT_UNCONTROLLED, c.now_ms - uncontrolled_since_ >= cfg_.uncontrolled_ms);
  }

  const ElectricHeaterConfig &config() const { return cfg_; }

 private:
  SignalIn *th100_, *th50_;
  ElectricHeaterConfig cfg_;
  uint32_t max_since_{0};
  uint32_t uncontrolled_since_{0};
};

}  // namespace vent
