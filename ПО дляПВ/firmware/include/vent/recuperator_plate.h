#pragma once
// Пластинчатый рекуператор (перекрёстный или противоточный) с электрическим
// преднагревателем. Защита от обмерзания по t_exhaust (выброс после рекуператора).
//
// Преднагреватель: вкл ниже preheat_on_below, выкл выше preheat_off_above (только при потоке).
// Лестница: шаг вниз — t_exhaust < frost_c дольше step_ms; шаг назад — > recover_c дольше recover_ms.
//  1) приток на ступень ниже (вытяжка как есть) — пока приток > 1;
//  2) вытяжка на ступень выше — пока вытяжка < максимума;
//  3) только вытяжка — не дольше exhaust_only_max_ms, затем стоп с аварией.
// forbid_imbalance (котёл/колонка/камин): приток и вытяжка снижаются вместе, без шагов 2–3;
// снижать некуда — стоп с аварией.

#include "alarms.h"
#include "control_math.h"
#include "recuperator.h"

namespace vent {

struct PlateRecuperatorConfig {
  float preheat_on_below{1.0f};
  float preheat_off_above{3.0f};
  float frost_c{0.0f};
  float recover_c{3.0f};
  uint32_t step_ms{600000};
  uint32_t recover_ms{600000};
  uint32_t exhaust_only_max_ms{1800000};
  bool forbid_imbalance{false};
  uint8_t max_level{3};  // максимальная ступень вентиляторов
};

// Скорости на уровне защиты level. last_level (если не nullptr) — номер последнего уровня.
inline FanRequest frost_ladder(FanRequest r, int level, bool forbid_imbalance, uint8_t max_level,
                               int *last_level) {
  if (r.supply == 0) {
    if (last_level) *last_level = 0;
    return r;
  }
  int s = r.supply, e = r.exhaust, lvl = 0;
  const int reduce_steps = s - 1;
  const int boost_steps = forbid_imbalance ? 0 : (int) max_level - e;
  const int total = reduce_steps + (boost_steps > 0 ? boost_steps : 0) + (forbid_imbalance ? 0 : 1);
  if (last_level) *last_level = total;
  for (; lvl < level && lvl < reduce_steps; lvl++) {
    s--;
    if (forbid_imbalance && e > 1) e--;
  }
  for (; lvl < level && lvl < reduce_steps + boost_steps; lvl++)
    e++;
  if (lvl < level && !forbid_imbalance)
    s = 0;
  r.supply = (uint8_t) s;
  r.exhaust = (uint8_t) e;
  return r;
}

class PlateRecuperator : public RecuperatorDriver {
 public:
  // preheater — SSR преднагревателя (nullptr — нет); preheat_thermostat — опц. термостат.
  PlateRecuperator(DigitalOut *preheater, SignalIn *preheat_thermostat, const PlateRecuperatorConfig &cfg)
      : pre_(preheater), pre_th_(preheat_thermostat), cfg_(cfg) {}

  const char *type_name() const override { return "Пластинчатый, преднагрев"; }
  bool needs_exhaust_sensor() const override { return true; }

  FanRequest adjust(const FanRequest &base) const override {
    return frost_ladder(base, level_, cfg_.forbid_imbalance, cfg_.max_level, nullptr);
  }

  void update(const RecupContext &c, AlarmSink &alarms) override {
    // --- преднагреватель ---
    const bool th_open = pre_th_ && !pre_th_->ok();
    alarms.set(AL_PREHEAT_THERMOSTAT, th_open);
    if (pre_ && c.airflow_ok && !th_open)
      preheat_on_ = hyst_.update(c.t_exhaust, cfg_.preheat_on_below, cfg_.preheat_off_above);
    else {
      hyst_.reset();
      preheat_on_ = false;
    }
    if (pre_)
      pre_->set(preheat_on_);

    // --- лестница ---
    update_ladder_(c);
    alarms.set(AL_FROST, trip_);
    const bool exhaust_only = c.base.supply > 0 && level_ > 0 && adjust(c.base).supply == 0;
    alarms.set(AL_FROST_EXHAUST_ONLY, exhaust_only && !trip_);
    alarms.set(AL_FROST_REDUCED, level_ > 0 && !exhaust_only);
  }

  bool preheater_active() const override { return preheat_on_; }
  int frost_level() const override { return level_; }

 private:
  void update_ladder_(const RecupContext &c) {
    const uint32_t now = c.now_ms;
    if (!c.allowed || !c.in_operation) {
      if (!c.allowed) {
        level_ = 0;
        trip_ = false;
      }
      cold_since_ = warm_since_ = exhaust_only_since_ = now;
      return;
    }
    int last = 0;
    frost_ladder(c.base, 0, cfg_.forbid_imbalance, cfg_.max_level, &last);
    if (level_ > last)
      level_ = last;  // режим уменьшили — шагов стало меньше
    const bool exhaust_only = level_ > 0 && adjust(c.base).supply == 0;
    const bool cold = !std::isnan(c.t_exhaust) && c.t_exhaust < cfg_.frost_c;
    const bool warm = !std::isnan(c.t_exhaust) && c.t_exhaust > cfg_.recover_c;

    if (!exhaust_only) {
      exhaust_only_since_ = now;
      if (!cold) {
        cold_since_ = now;
      } else if (now - cold_since_ >= cfg_.step_ms) {
        cold_since_ = now;
        if (level_ < last)
          level_++;
        else
          trip_ = true;
      }
    } else if (now - exhaust_only_since_ >= cfg_.exhaust_only_max_ms) {
      trip_ = true;
    }

    if (level_ > 0 && warm) {
      if (now - warm_since_ >= cfg_.recover_ms) {
        warm_since_ = now;
        level_--;
        cold_since_ = now;
      }
    } else {
      warm_since_ = now;
    }
  }

  DigitalOut *pre_;
  SignalIn *pre_th_;
  PlateRecuperatorConfig cfg_;
  Hysteresis hyst_{};
  bool preheat_on_{false};
  int level_{0};
  bool trip_{false};
  uint32_t cold_since_{0}, warm_since_{0}, exhaust_only_since_{0};
};

}  // namespace vent
