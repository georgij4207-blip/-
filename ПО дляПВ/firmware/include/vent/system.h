#pragma once
// Верхний уровень логики установки. Профиль = тип установки + набор драйверов + параметры.
//
// Система отвечает за то, что общее для любой установки:
//   режим и последовательность пуска/останова (sequencer.h), пожар, внешний стоп,
//   датчики, перегрев притока, поток, фильтры, связь с платой, журнал, «норма»/«работа».
// Драйверы — за своё оборудование и его защиты: вентиляторы (fan_*.h), нагрев (heater_*.h),
// рекуператор и обмерзание (recuperator_*.h), заслонка (damper_*.h), охлаждение (cooling_*.h).
//
// Без ESPHome: та же логика собирается на хосте для тестов и симуляции.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "alarms.h"
#include "auto_speed.h"
#include "cooling.h"
#include "damper.h"
#include "fan.h"
#include "heater.h"
#include "recuperator.h"
#include "recuperator_none.h"
#include "sequencer.h"

namespace vent {

static_assert(AL_COUNT <= 32, "маска аварий — 32 бита");

// snprintf режет по байтам; если обрезка пришлась на середину символа UTF-8 (кириллица —
// 2 байта), убираем неполный хвост, чтобы строка оставалась корректной.
inline void utf8_fix_tail(char *buf) {
  size_t len = strlen(buf);
  size_t i = len;
  while (i > 0 && ((unsigned char) buf[i - 1] & 0xC0) == 0x80) i--;  // байты продолжения
  if (i == 0) return;
  const unsigned char lead = (unsigned char) buf[i - 1];
  const size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
  if (len - (i - 1) < need) buf[i - 1] = '\0';
}

enum class UnitType : uint8_t {
  SUPPLY,          // приточная (часто водяной калорифер)
  SUPPLY_EXHAUST,  // приточно-вытяжная с рекуператором (Salda)
  EXHAUST,         // вытяжная
};

inline const char *unit_type_text(UnitType t) {
  switch (t) {
    case UnitType::SUPPLY: return "приточная";
    case UnitType::SUPPLY_EXHAUST: return "приточно-вытяжная";
    case UnitType::EXHAUST: return "вытяжная";
    default: return "?";
  }
}

struct VentConfig {
  UnitType unit_type{UnitType::SUPPLY_EXHAUST};
  SequencerConfig seq{};
  uint8_t fan_levels{3};          // ступени в режиме (Стоп/1…fan_levels)
  uint32_t startup_ms{10000};     // после загрузки всё выключено, аварии не оцениваются
  float overheat_c{45.0f};
  float purge_hold_c{35.0f};      // продувку не заканчивать, пока приток горячее
  float outdoor_min_c{-35.0f};
  float outdoor_hyst_c{2.0f};
  uint32_t flow_check_ms{30000};  // приток работает столько, а потока нет → авария
  uint32_t flow_stuck_ms{60000};  // приток стоит столько, а поток «есть» → предупреждение
  bool fire_purge{false};         // пожар: false — стоп без продувки, true — с продувкой
  AutoSpeedConfig auto_speed{};   // авторежим по CO2/влажности
};

// Общие входы системы. Входы оборудования (термостаты ТЭНа и т.п.) — у драйверов.
static const uint8_t MODE_AUTO = 255;  // режим «Авто»: ступень по CO2/влажности

struct VentInputs {
  uint8_t mode{0};  // 0 = стоп, 1…fan_levels, MODE_AUTO
  int exhaust_offset{0};
  float t_outdoor{NAN}, t_supply{NAN}, t_extract{NAN}, t_exhaust{NAN};
  bool fire_ok{true};
  bool flow_ok{false};  // реле давления притока (нет реле — профиль подаёт true)
  bool ext_stop_ok{true};
  bool filter_supply_ok{true};
  bool filter_exhaust_ok{true};
  bool io_ok{true};
  bool heating_enabled{true};  // «Нагрев (зима)»
  bool cooling_enabled{false};  // «Охлаждение (лето)»
  float setpoint{18.0f};
  uint32_t unix_time{0};
  float co2_ppm{NAN};   // для авторежима (нет датчика — NAN)
  float humidity{NAN};  // %, для подъёма ступени по влажности
};

struct VentStatus {
  uint8_t supply_level{0};
  uint8_t exhaust_level{0};
  bool damper_open{false};
  float heater_pct{0.0f};
  float cooler_pct{0.0f};
  bool preheater{false};
  bool ok{false};       // «норма»: нет аварий
  bool running{false};  // «работа»: вентиляторы в работе
  bool auto_mode{false};    // включён авторежим
  uint8_t auto_level{0};    // ступень, выбранная авторежимом
};

// Обрезать строку UTF-8 до max_chars символов (для экрана: ширина в символах, не байтах).
inline void utf8_truncate_chars(char *buf, size_t max_chars) {
  size_t chars = 0;
  for (size_t i = 0; buf[i]; i++) {
    if (((unsigned char) buf[i] & 0xC0) != 0x80) {  // начало символа
      if (chars == max_chars) {
        buf[i] = '\0';
        return;
      }
      chars++;
    }
  }
}

class VentSystem : private AlarmSink {
 public:
  // --- профиль: драйверы (не владеет; nullptr — оборудования нет) ---
  void set_supply_fan(FanDriver *d) { supply_ = d; }
  void set_exhaust_fan(FanDriver *d) { exhaust_ = d; }
  void set_heater(HeaterDriver *d) { heater_ = d; }
  void set_recuperator(RecuperatorDriver *d) { recup_ = d; }
  void set_damper(DamperDriver *d) { damper_ = d; }
  void set_cooling(CoolingDriver *d) { cooler_ = d; }
  void set_alarm_output(DigitalOut *o) { ok_out_ = o; }   // сухой контакт «норма» (fail-safe)
  void set_run_output(DigitalOut *o) { run_out_ = o; }    // сухой контакт «работа»

  // Вызывать после регистрации драйверов.
  void configure(const VentConfig &cfg) {
    cfg_ = cfg;
    if (!recup_)
      recup_ = &no_recup_;
    seq_.configure(cfg.seq, supply_, exhaust_, damper_);
    auto_.configure(cfg.auto_speed);
    validate_();
  }

  const VentStatus &update(uint32_t now, const VentInputs &in) {
    const float dt_s = last_ms_ ? (now - last_ms_) / 1000.0f : 0.0f;
    last_ms_ = now;
    now_s_ = now / 1000;
    unix_ = in.unix_time;

    if (now < cfg_.startup_ms) {
      seq_.update(now, FanRequest{});
      status_ = VentStatus{};
      write_status_outputs_();
      return status_;
    }

    // --- 1. Общие причины аварий ---
    set(AL_CONFIG, !config_ok_);
    set(AL_FIRE, !in.fire_ok);
    set(AL_EXT_STOP, !in.ext_stop_ok);
    set(AL_IO_FAULT, !in.io_ok);
    const bool has_supply = supply_ != nullptr;
    set(AL_SENSOR_OUTDOOR, has_supply && std::isnan(in.t_outdoor));
    set(AL_SENSOR_SUPPLY, has_supply && std::isnan(in.t_supply));
    set(AL_SENSOR_EXHAUST, recup_->needs_exhaust_sensor() && std::isnan(in.t_exhaust));
    set(AL_SENSOR_EXTRACT, exhaust_ != nullptr && std::isnan(in.t_extract));
    set(AL_OVERHEAT, has_supply && !std::isnan(in.t_supply) && in.t_supply > cfg_.overheat_c);
    if (has_supply && !std::isnan(in.t_outdoor)) {
      if (in.t_outdoor < cfg_.outdoor_min_c)
        outdoor_low_ = true;
      else if (in.t_outdoor > cfg_.outdoor_min_c + cfg_.outdoor_hyst_c)
        outdoor_low_ = false;
    }
    set(AL_OUTDOOR_LOW, outdoor_low_);
    set(AL_FILTER_SUPPLY, has_supply && !in.filter_supply_ok);
    set(AL_FILTER_EXHAUST, exhaust_ != nullptr && !in.filter_exhaust_ok);

    set(AL_NO_FLOW, seq_.supply_running_ms(now) >= cfg_.flow_check_ms && !in.flow_ok);
    if (seq_.supply_level() > 0 || !has_supply)
      supply_off_since_ = now;
    set(AL_FLOW_STUCK, now - supply_off_since_ >= cfg_.flow_stuck_ms && in.flow_ok);

    if (supply_) supply_->report(*this);
    if (exhaust_) exhaust_->report(*this);
    if (damper_) damper_->update(now, *this);

    const bool fire = alarms_.on(AL_FIRE);
    const bool cool_down = alarms_.any_action(AlarmAction::COOL);
    const bool stop = alarms_.any_action(AlarmAction::STOP) || (fire && cfg_.fire_purge);
    const bool start_ok = !heater_ || heater_->start_permitted();

    // --- 2. Скорости: режим (или авторежим) → рекуператор (обмерзание) → аварии ---
    uint8_t mode = in.mode;
    const bool auto_mode = in.mode == MODE_AUTO;
    if (auto_mode) {
      mode = auto_.update(now, in.co2_ppm, in.humidity, cfg_.fan_levels);
    } else {
      auto_.reset();
      if (mode > cfg_.fan_levels) mode = cfg_.fan_levels;
    }
    set(AL_CO2_SENSOR, auto_mode && auto_.sensor_missing());
    const FanRequest base = speeds_from_mode(mode, in.exhaust_offset, cfg_.fan_levels);
    FanRequest req;
    if (fire && !cfg_.fire_purge) {
      req.emergency_stop = true;
      req.close_damper_now = true;
    } else if (cool_down) {
      req.supply = cfg_.fan_levels;  // нагрев «сам по себе»: максимум воздуха, не останавливать
      req.exhaust = cfg_.fan_levels;
    } else if (!stop && mode > 0 && start_ok) {
      req = recup_->adjust(base);
    }
    req.hold_purge = has_supply && !std::isnan(in.t_supply) && in.t_supply > cfg_.purge_hold_c;
    seq_.update(now, req);

    // --- 3. Рекуператор: обмерзание и преднагрев ---
    const bool airflow_ok = seq_.heat_permitted(now) && in.flow_ok && !stop && !fire && !cool_down;
    RecupContext rc;
    rc.now_ms = now;
    rc.allowed = !stop && !fire && !cool_down && mode > 0;
    rc.in_operation = seq_.in_operation();
    rc.airflow_ok = airflow_ok;
    rc.base = base;
    rc.t_outdoor = in.t_outdoor;
    rc.t_extract = in.t_extract;
    rc.t_exhaust = in.t_exhaust;
    recup_->update(rc, *this);

    // --- 4. Нагрев ---
    if (heater_) {
      HeatContext hc;
      hc.now_ms = now;
      hc.dt_s = dt_s;
      hc.heat_allowed = in.heating_enabled && !stop && !fire && !cool_down &&
                        !alarms_.any_action(AlarmAction::BLOCK_HEATER);
      hc.airflow_ok = airflow_ok || !heater_->needs_airflow();
      hc.unit_running = seq_.in_operation();
      hc.supply_running_ms = seq_.supply_running_ms(now);
      hc.preheat_active = recup_->preheater_active();
      hc.setpoint = in.setpoint;
      hc.t_supply = in.t_supply;
      hc.t_outdoor = in.t_outdoor;
      hc.t_extract = in.t_extract;
      heater_->update(hc, *this);
    }

    // --- 5. Охлаждение ---
    if (cooler_) {
      CoolContext cc;
      cc.now_ms = now;
      cc.dt_s = dt_s;
      cc.cool_allowed = in.cooling_enabled && !in.heating_enabled && !stop && !fire && !cool_down;
      cc.airflow_ok = airflow_ok;
      cc.setpoint = in.setpoint;
      cc.t_supply = in.t_supply;
      cc.t_outdoor = in.t_outdoor;
      cc.t_extract = in.t_extract;
      cooler_->update(cc, *this);
    }

    // --- 6. Состояние ---
    status_.supply_level = seq_.supply_level();
    status_.exhaust_level = seq_.exhaust_level();
    status_.damper_open = damper_ && damper_->commanded_open();
    status_.heater_pct = heater_ ? heater_->output_pct() : 0.0f;
    status_.cooler_pct = cooler_ ? cooler_->output_pct() : 0.0f;
    status_.preheater = recup_->preheater_active();
    status_.ok = !alarms_.any_fault();
    status_.running = seq_.in_operation();
    status_.auto_mode = auto_mode;
    status_.auto_level = auto_mode ? mode : 0;
    write_status_outputs_();
    return status_;
  }

  void reset_alarms(uint32_t now, uint32_t unix_time) { alarms_.reset(now / 1000, unix_time); }

  const VentStatus &status() const { return status_; }
  const FanSequencer &fans() const { return seq_; }
  Alarms &alarms() { return alarms_; }
  const Alarms &alarms() const { return alarms_; }
  const RecuperatorDriver &recuperator() const { return *recup_; }
  int frost_level() const { return recup_->frost_level(); }
  const AutoSpeed &auto_speed() const { return auto_; }
  bool config_ok() const { return config_ok_; }
  const char *config_error() const { return config_error_; }

  // Описание профиля для интерфейса и лога.
  void profile_text(char *buf, size_t n) const {
    snprintf(buf, n, "%s; приток: %s; вытяжка: %s; нагрев: %s; рекуператор: %s; заслонка: %s; охлаждение: %s",
             unit_type_text(cfg_.unit_type), supply_ ? supply_->type_name() : "нет",
             exhaust_ ? exhaust_->type_name() : "нет", heater_ ? heater_->type_name() : "нет",
             recup_->type_name(), damper_ ? damper_->type_name() : "нет",
             cooler_ ? cooler_->type_name() : "нет");
    utf8_fix_tail(buf);
  }

  void status_text(char *buf, size_t n) const {
    const uint8_t top = alarms_.top();
    if (!config_ok_)
      snprintf(buf, n, "Авария: %s", config_error_);
    else if (top != AL_NONE && alarm_def(top).fault)
      snprintf(buf, n, "Авария: %s", alarm_def(top).text);
    else if (alarms_.on(AL_EXT_STOP))
      snprintf(buf, n, "Внешний стоп (%s)", seq_.state_text());
    else if (top != AL_NONE)
      snprintf(buf, n, "%s (%s)", seq_.state_text(), alarm_def(top).text);
    else
      snprintf(buf, n, "%s", seq_.state_text());
    utf8_fix_tail(buf);
  }

 private:
  void set(uint8_t code, bool condition) override { alarms_.set(code, condition, now_s_, unix_); }

  void write_status_outputs_() {
    if (ok_out_) ok_out_->set(status_.ok);
    if (run_out_) run_out_->set(status_.running);
  }

  // Проверка профиля: каждое найденное несоответствие — авария конфигурации, установка не пускается.
  void validate_() {
    config_ok_ = false;
    const bool need_supply = cfg_.unit_type != UnitType::EXHAUST;
    const bool need_exhaust = cfg_.unit_type != UnitType::SUPPLY;
    if (need_supply && !supply_) { config_error_ = "нет драйвера приточного вентилятора"; return; }
    if (need_exhaust && !exhaust_) { config_error_ = "нет драйвера вытяжного вентилятора"; return; }
    if (!need_supply && supply_) { config_error_ = "у вытяжной установки задан приточный вентилятор"; return; }
    if (!need_exhaust && exhaust_) { config_error_ = "у приточной установки задан вытяжной вентилятор"; return; }
    if (supply_ && !supply_->implemented()) { config_error_ = "драйвер приточного вентилятора не реализован"; return; }
    if (exhaust_ && !exhaust_->implemented()) { config_error_ = "драйвер вытяжного вентилятора не реализован"; return; }
    if (heater_ && !heater_->implemented()) { config_error_ = "драйвер нагревателя не реализован"; return; }
    if (heater_ && !supply_) { config_error_ = "нагреватель без приточного вентилятора"; return; }
    if (!recup_->implemented()) { config_error_ = "драйвер рекуператора не реализован"; return; }
    if (recup_ != &no_recup_ && cfg_.unit_type != UnitType::SUPPLY_EXHAUST) {
      config_error_ = "рекуператор возможен только у приточно-вытяжной установки";
      return;
    }
    if (damper_ && !damper_->implemented()) { config_error_ = "драйвер заслонки не реализован"; return; }
    if (cooler_ && !cooler_->implemented()) { config_error_ = "драйвер охлаждения не реализован"; return; }
    if (cooler_ && !supply_) { config_error_ = "охлаждение без приточного вентилятора"; return; }
    if ((supply_ && supply_->levels() < cfg_.fan_levels) || (exhaust_ && exhaust_->levels() < cfg_.fan_levels)) {
      config_error_ = "у вентилятора меньше ступеней, чем в режиме";
      return;
    }
    config_ok_ = true;
    config_error_ = "нет";
  }

  VentConfig cfg_{};
  FanDriver *supply_{nullptr};
  FanDriver *exhaust_{nullptr};
  HeaterDriver *heater_{nullptr};
  RecuperatorDriver *recup_{nullptr};
  DamperDriver *damper_{nullptr};
  CoolingDriver *cooler_{nullptr};
  DigitalOut *ok_out_{nullptr};
  DigitalOut *run_out_{nullptr};
  NoRecuperator no_recup_{};
  FanSequencer seq_{};
  AutoSpeed auto_{};
  Alarms alarms_{};
  VentStatus status_{};
  bool config_ok_{false};
  const char *config_error_{"не настроено"};
  uint32_t last_ms_{0};
  uint32_t now_s_{0};
  uint32_t unix_{0};
  bool outdoor_low_{false};
  uint32_t supply_off_since_{0};
};

// Экземпляр для прошивки (globals ESPHome объявляются раньше includes).
inline VentSystem &vent_system() {
  static VentSystem instance;
  return instance;
}

}  // namespace vent
