#pragma once
// EC-вентилятор с управлением 0–10 В: ступень → % по таблице профиля, плавный разгон,
// выход «разрешение» (пуск), вход «авария» и тахо (опционально).
//
// Пуск: «разрешение» вкл и 0–10 В плавно до уставки ступени (ramp_pct_per_s). Смена ступени —
// плавно в обе стороны. Стоп: 0 В и «разрешение» выкл сразу (мотор выбегает сам).
// settled() — уставка достигнута: до этого последовательность не разрешает нагрев.
// Авария (код профиля: AL_FAN_SUPPLY / AL_FAN_EXHAUST): вход «авария» разомкнут дольше
// fault_ms, или мотор на уставке дольше tacho_check_ms, а обороты ниже tacho_min_rpm.

#include <cmath>

#include "fan.h"

namespace vent {

struct EcAnalogFanConfig {
  static const uint8_t MAX_LEVELS = 5;
  uint8_t levels{3};
  float level_pct[MAX_LEVELS]{40.0f, 65.0f, 90.0f, 100.0f, 100.0f};  // 0–10 В = 0–100 %
  float ramp_pct_per_s{10.0f};
  uint32_t fault_ms{2000};
  uint32_t tacho_check_ms{30000};
  float tacho_min_rpm{300.0f};  // ниже на уставке → авария (если тахо подключено)
};

class EcAnalogFan : public FanDriver {
 public:
  // out — 0–10 В (доля 0…1); enable — «разрешение» (nullptr — нет); fault — вход «авария EC»
  // (nullptr — нет); rpm — обороты (nullptr — нет тахо); alarm_code — AL_FAN_SUPPLY / AL_FAN_EXHAUST.
  EcAnalogFan(AnalogOut *out, DigitalOut *enable, SignalIn *fault, AnalogIn *rpm, uint8_t alarm_code,
              const EcAnalogFanConfig &cfg)
      : out_(out), enable_(enable), fault_(fault), rpm_(rpm), code_(alarm_code), cfg_(cfg) {
    if (cfg_.levels > EcAnalogFanConfig::MAX_LEVELS) cfg_.levels = EcAnalogFanConfig::MAX_LEVELS;
  }

  const char *type_name() const override { return "EC 0–10 В"; }
  uint8_t levels() const override { return cfg_.levels; }
  void set_target(uint8_t level) override { target_ = level > cfg_.levels ? cfg_.levels : level; }

  void update(uint32_t now) override {
    const float dt = init_ ? (now - now_) / 1000.0f : 0.0f;
    now_ = now;
    init_ = true;
    if (target_ == 0) {
      pct_ = 0.0f;
    } else {
      const float goal = cfg_.level_pct[target_ - 1];
      const float step = cfg_.ramp_pct_per_s * dt;
      if (pct_ < goal) pct_ = pct_ + step > goal ? goal : pct_ + step;
      else if (pct_ > goal) pct_ = pct_ - step < goal ? goal : pct_ - step;
    }
    if (enable_) enable_->set(target_ > 0);
    out_->set(pct_ / 100.0f);
    if (!settled() || target_ == 0)
      at_goal_since_ = now;
  }

  uint8_t active_level() const override { return target_; }
  bool settled() const override { return target_ == 0 || pct_ == cfg_.level_pct[target_ - 1]; }
  float output_pct() const { return pct_; }

  void report(AlarmSink &alarms) override {
    const bool fault_open = fault_ && !fault_->ok();
    if (!fault_open) fault_since_ = now_;
    bool tacho_bad = false;
    if (rpm_ && target_ > 0 && now_ - at_goal_since_ >= cfg_.tacho_check_ms) {
      const float r = rpm_->value();
      tacho_bad = std::isnan(r) || r < cfg_.tacho_min_rpm;
    }
    alarms.set(code_, (fault_open && now_ - fault_since_ >= cfg_.fault_ms) || tacho_bad);
  }

 private:
  AnalogOut *out_;
  DigitalOut *enable_;
  SignalIn *fault_;
  AnalogIn *rpm_;
  uint8_t code_;
  EcAnalogFanConfig cfg_;
  uint8_t target_{0};
  float pct_{0.0f};
  uint32_t now_{0};
  bool init_{false};
  uint32_t fault_since_{0};
  uint32_t at_goal_since_{0};
};

}  // namespace vent
