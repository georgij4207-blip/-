#pragma once
// Верхний уровень логики установки: вентиляция + нагрев + защиты.
// Вход — VentInputs (датчики уже пересчитаны в °C, NAN = недостоверно; входы уже
// приведены к «норма = true» с учётом NC/NO). Выход — VentOutputs для реле и SSR.
// Без ESPHome: та же логика собирается на хосте для тестов и симуляции.

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "vent_alarms.h"
#include "vent_fans.h"
#include "vent_heat.h"

namespace vent {

struct VentConfig {
  FanConfig fan{};
  uint32_t startup_ms{10000};  // после загрузки всё выключено, аварии не оцениваются

  // Нагрев
  float heater_out_max{100.0f};        // %
  float heat_full_pct{98.0f};          // «ТЭН на максимуме»
  uint32_t heat_insufficient_ms{900000};
  float heat_insufficient_band{2.0f};  // °C ниже уставки
  float preheat_on_below{1.0f};        // t_exhaust, °C
  float preheat_off_above{3.0f};

  // Защиты
  float overheat_c{45.0f};
  float outdoor_min_c{-35.0f};
  float outdoor_hyst_c{2.0f};
  uint32_t flow_check_ms{30000};   // приток работает столько, а потока нет → авария
  uint32_t flow_stuck_ms{60000};   // приток стоит столько, а поток «есть» → предупреждение
  bool fire_purge{false};          // пожар: false — стоп без продувки, true — с продувкой

  // Обмерзание
  float frost_c{0.0f};                  // t_exhaust ниже — обмерзание
  float frost_recover_c{3.0f};          // t_exhaust выше — оттаяло
  uint32_t frost_step_ms{600000};       // столько ниже frost_c → снизить приток на ступень
  uint32_t frost_recover_ms{600000};    // столько выше frost_recover_c → вернуть ступень
  uint32_t exhaust_only_max_ms{1800000};
  bool exhaust_only_forbidden{false};   // котёл/колонка/камин: без притока не работать
};

struct VentInputs {
  uint8_t mode{0};  // 0 = стоп, 1..3
  int exhaust_offset{0};
  float t_outdoor{NAN}, t_supply{NAN}, t_extract{NAN}, t_exhaust{NAN};
  bool fire_ok{true};
  bool thermostat_ok{true};
  bool thermostat50_ok{true};
  bool preheat_thermostat_ok{true};
  bool flow_ok{false};
  bool ext_stop_ok{true};
  bool filter_supply_ok{true};
  bool filter_exhaust_ok{true};
  bool io_ok{true};  // связь с платами входов/реле
  bool heating_enabled{true};
  float setpoint{18.0f};
  float kp{10.0f};     // %/°C
  float ti_s{120.0f};  // с
  uint32_t unix_time{0};
};

struct VentOutputs {
  uint8_t supply_mask{0};
  uint8_t exhaust_mask{0};
  bool damper{false};
  float heater_pct{0.0f};
  bool preheater{false};
  bool ok{false};  // K8 «норма»: нет аварий
};

class VentSystem {
 public:
  void configure(const VentConfig &cfg) {
    cfg_ = cfg;
    fans_.configure(cfg.fan);
  }

  const VentOutputs &update(uint32_t now, const VentInputs &in) {
    const float dt_s = last_ms_ ? (now - last_ms_) / 1000.0f : 0.0f;
    last_ms_ = now;
    const uint32_t up_s = now / 1000;
    auto set = [&](uint8_t code, bool cond) { alarms_.set(code, cond, up_s, in.unix_time); };

    if (now < cfg_.startup_ms) {
      fans_.update(now, FanRequest{});
      out_ = VentOutputs{};
      return out_;
    }

    // --- 1. Причины аварий ---
    set(AL_FIRE, !in.fire_ok);
    set(AL_THERMOSTAT, !in.thermostat_ok);
    set(AL_SENSOR_OUTDOOR, std::isnan(in.t_outdoor));
    set(AL_SENSOR_SUPPLY, std::isnan(in.t_supply));
    set(AL_SENSOR_EXHAUST, std::isnan(in.t_exhaust));
    set(AL_SENSOR_EXTRACT, std::isnan(in.t_extract));
    set(AL_OVERHEAT, !std::isnan(in.t_supply) && in.t_supply > cfg_.overheat_c);
    if (!std::isnan(in.t_outdoor)) {
      if (in.t_outdoor < cfg_.outdoor_min_c)
        outdoor_low_ = true;
      else if (in.t_outdoor > cfg_.outdoor_min_c + cfg_.outdoor_hyst_c)
        outdoor_low_ = false;
    }
    set(AL_OUTDOOR_LOW, outdoor_low_);
    set(AL_EXT_STOP, !in.ext_stop_ok);
    set(AL_FILTER_SUPPLY, !in.filter_supply_ok);
    set(AL_FILTER_EXHAUST, !in.filter_exhaust_ok);
    set(AL_THERMOSTAT50, !in.thermostat50_ok);
    set(AL_PREHEAT_THERMOSTAT, !in.preheat_thermostat_ok);
    set(AL_IO_FAULT, !in.io_ok);

    const uint32_t supply_run_ms = fans_.supply_running_ms(now);
    set(AL_NO_FLOW, supply_run_ms >= cfg_.flow_check_ms && !in.flow_ok);
    if (fans_.supply_speed() > 0)
      supply_off_since_ = now;
    set(AL_FLOW_STUCK, now - supply_off_since_ >= cfg_.flow_stuck_ms && in.flow_ok);

    const bool fire = alarms_.on(AL_FIRE);
    const bool stop = alarms_.any_action(AlarmAction::STOP) || (fire && cfg_.fire_purge);

    // --- 2. Обмерзание ---
    update_frost_(now, in, !stop && !fire && in.mode > 0);
    set(AL_FROST, frost_trip_);
    const bool exhaust_only = frost_level_ > 0 && frost_level_ >= in.mode && in.mode > 0;
    set(AL_FROST_EXHAUST_ONLY, exhaust_only && !frost_trip_);
    set(AL_FROST_REDUCED, frost_level_ > 0 && !exhaust_only);

    // --- 3. Вентиляция ---
    FanRequest req;
    if (fire && !cfg_.fire_purge) {
      req.emergency_stop = true;
      req.close_damper_now = true;
    } else if (!stop && in.mode > 0) {
      req = speeds_from_mode(in.mode, in.exhaust_offset);
      const int supply = (int) in.mode - frost_level_;
      req.supply = supply > 0 ? (uint8_t) supply : 0;
    }
    fans_.update(now, req);

    // --- 4. Нагрев ---
    const bool airflow_ok = fans_.heat_permitted(now) && in.flow_ok && !stop && !fire;
    const bool heater_ok = airflow_ok && in.heating_enabled &&
                           !alarms_.any_action(AlarmAction::BLOCK_HEATER) &&
                           !std::isnan(in.t_supply);
    if (heater_ok) {
      out_.heater_pct = pi_.update(in.setpoint, in.t_supply, in.kp, in.ti_s, cfg_.heater_out_max, dt_s);
    } else {
      pi_.reset();
      out_.heater_pct = 0.0f;
    }

    const bool preheater_ok = airflow_ok && !alarms_.any_action(AlarmAction::BLOCK_PREHEATER);
    if (preheater_ok) {
      out_.preheater = preheat_.update(in.t_exhaust, cfg_.preheat_on_below, cfg_.preheat_off_above);
    } else {
      preheat_.reset();
      out_.preheater = false;
    }

    const bool at_max = heater_ok && out_.heater_pct >= cfg_.heat_full_pct &&
                        in.t_supply < in.setpoint - cfg_.heat_insufficient_band;
    if (!at_max)
      heat_max_since_ = now;
    set(AL_HEAT_INSUFFICIENT, now - heat_max_since_ >= cfg_.heat_insufficient_ms);

    // --- 5. Выходы ---
    out_.supply_mask = fans_.supply_relay_mask();
    out_.exhaust_mask = fans_.exhaust_relay_mask();
    out_.damper = fans_.damper_open_cmd();
    out_.ok = !alarms_.any_fault();
    return out_;
  }

  void reset_alarms(uint32_t now, uint32_t unix_time) { alarms_.reset(now / 1000, unix_time); }

  const VentOutputs &outputs() const { return out_; }
  const FanController &fans() const { return fans_; }
  Alarms &alarms() { return alarms_; }
  const Alarms &alarms() const { return alarms_; }
  const PiController &pi() const { return pi_; }
  int frost_level() const { return frost_level_; }

  // Строка состояния для интерфейса.
  void status_text(char *buf, size_t n) const {
    const uint8_t top = alarms_.top();
    if (top != AL_NONE && alarm_def(top).fault)
      snprintf(buf, n, "Авария: %s", alarm_def(top).text);
    else if (alarms_.on(AL_EXT_STOP))
      snprintf(buf, n, "Внешний стоп (%s)", fans_.state_text());
    else if (top != AL_NONE)
      snprintf(buf, n, "%s (%s)", fans_.state_text(), alarm_def(top).text);
    else
      snprintf(buf, n, "%s", fans_.state_text());
  }

 private:
  void update_frost_(uint32_t now, const VentInputs &in, bool allowed) {
    const FanState fs = fans_.state();
    const bool in_operation =
        fs == FanState::RUNNING || fs == FanState::PURGE || fs == FanState::EXHAUST_ONLY;
    if (!allowed || !in_operation) {
      if (!allowed) {
        frost_level_ = 0;
        frost_trip_ = false;
      }
      cold_since_ = warm_since_ = exhaust_only_since_ = now;
      return;
    }
    const bool exhaust_only = frost_level_ >= in.mode;
    const bool cold = !std::isnan(in.t_exhaust) && in.t_exhaust < cfg_.frost_c;
    const bool warm = !std::isnan(in.t_exhaust) && in.t_exhaust > cfg_.frost_recover_c;

    if (!exhaust_only) {
      exhaust_only_since_ = now;
      if (!cold) {
        cold_since_ = now;
      } else if (now - cold_since_ >= cfg_.frost_step_ms) {
        cold_since_ = now;
        frost_level_++;
        if (frost_level_ >= in.mode && cfg_.exhaust_only_forbidden)
          frost_trip_ = true;  // разрежение в помещении недопустимо — полный стоп
      }
    } else if (now - exhaust_only_since_ >= cfg_.exhaust_only_max_ms) {
      frost_trip_ = true;  // оттайка вытяжкой не помогла
    }

    if (frost_level_ > 0 && warm) {
      if (now - warm_since_ >= cfg_.frost_recover_ms) {
        warm_since_ = now;
        frost_level_--;
        cold_since_ = now;
      }
    } else {
      warm_since_ = now;
    }
  }

  VentConfig cfg_{};
  FanController fans_{};
  PiController pi_{};
  Hysteresis preheat_{};
  Alarms alarms_{};
  VentOutputs out_{};
  uint32_t last_ms_{0};
  bool outdoor_low_{false};
  uint32_t supply_off_since_{0};
  uint32_t heat_max_since_{0};
  int frost_level_{0};
  bool frost_trip_{false};
  uint32_t cold_since_{0}, warm_since_{0}, exhaust_only_since_{0};
};

// Экземпляр для прошивки (globals ESPHome объявляются раньше includes).
inline VentSystem &vent_system() {
  static VentSystem instance;
  return instance;
}

}  // namespace vent
