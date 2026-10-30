#pragma once
// Вентиляторы: последовательность пуска/останова и драйвер AC-мотора с отводами.
// Чистая логика без ESPHome: время передаётся снаружи (мс), выходы читаются геттерами.
//
// Пуск:   [заслонка открыть → ждать] → вытяжка → ждать → приток → РАБОТА
//         (нагрев разрешается через heat_delay после того, как приток реально работает)
// Останов: нагрев запрещён сразу → продувка притоком → стоп притока → ждать →
//          стоп вытяжки → выбег → [заслонка закрыть]

#include <cstdint>

namespace vent {

// --- Привод AC с отводами обмотки: одна ступень = одно реле ---
// Смена ступени всегда через «всё выключено» и паузу.
class TapSpeedDriver {
 public:
  void set_pause_ms(uint32_t ms) { pause_ms_ = ms; }
  void set_target(uint8_t speed) { target_ = speed > 3 ? 3 : speed; }

  void update(uint32_t now) {
    if (active_ == target_)
      return;
    if (active_ != 0) {
      active_ = 0;
      off_since_ = now;
      pause_pending_ = true;
      return;
    }
    if (pause_pending_ && now - off_since_ < pause_ms_)
      return;
    pause_pending_ = false;
    active_ = target_;
  }

  uint8_t target() const { return target_; }
  uint8_t active() const { return active_; }  // ступень, на которую сейчас подана фаза
  bool settled() const { return active_ == target_; }
  // Бит 0 — ступень 1, бит 1 — ступень 2, бит 2 — ступень 3. Не более одного бита.
  uint8_t relay_mask() const { return active_ ? (uint8_t) (1u << (active_ - 1)) : 0; }

 private:
  uint8_t target_{0};
  uint8_t active_{0};
  uint32_t pause_ms_{1500};
  uint32_t off_since_{0};
  bool pause_pending_{false};  // после выключения ступени ещё идёт пауза
};

// --- Запрос скоростей (политика режима отдельно от последовательности) ---
struct FanRequest {
  uint8_t supply{0};   // 0 = стоп, 1..3
  uint8_t exhaust{0};  // 0..3; при supply > 0 должна быть > 0
  bool emergency_stop{false};  // немедленный стоп без продувки (шаг 5: защиты)
};

inline uint8_t clamp_speed(int s) { return s < 1 ? 1 : (s > 3 ? 3 : (uint8_t) s); }

// Общий режим + смещение вытяжки. Для раздельной уставки достаточно
// сформировать FanRequest иначе — последовательность от этого не меняется.
inline FanRequest speeds_from_mode(uint8_t mode, int exhaust_offset) {
  FanRequest r;
  if (mode == 0)
    return r;
  r.supply = clamp_speed(mode);
  r.exhaust = clamp_speed(mode + exhaust_offset);
  return r;
}

struct FanConfig {
  bool damper_enabled{true};
  uint32_t damper_open_ms{30000};
  uint32_t exhaust_lead_ms{3000};  // вытяжка раньше притока при пуске, позже при останове
  uint32_t purge_ms{120000};
  uint32_t heat_delay_ms{10000};
  uint32_t rundown_ms{15000};      // выбег до закрытия заслонки
  uint32_t switch_pause_ms{1500};
};

enum class FanState : uint8_t {
  STOPPED,
  DAMPER_OPENING,
  START_EXHAUST,
  RUNNING,
  PURGE,
  STOP_EXHAUST,
  RUNDOWN,
};

inline const char *fan_state_text(FanState s) {
  switch (s) {
    case FanState::STOPPED: return "Стоп";
    case FanState::DAMPER_OPENING: return "Открытие заслонки";
    case FanState::START_EXHAUST: return "Пуск вытяжки";
    case FanState::RUNNING: return "Работа";
    case FanState::PURGE: return "Продувка";
    case FanState::STOP_EXHAUST: return "Останов вытяжки";
    case FanState::RUNDOWN: return "Выбег";
    default: return "?";
  }
}

class FanController {
 public:
  void configure(const FanConfig &cfg) {
    cfg_ = cfg;
    supply_.set_pause_ms(cfg.switch_pause_ms);
    exhaust_.set_pause_ms(cfg.switch_pause_ms);
  }

  void update(uint32_t now, const FanRequest &req) {
    const bool run = req.supply > 0 && !req.emergency_stop;

    if (req.emergency_stop && state_ != FanState::STOPPED && state_ != FanState::RUNDOWN) {
      supply_.set_target(0);
      exhaust_.set_target(0);
      enter_(FanState::RUNDOWN, now);
    }

    switch (state_) {
      case FanState::STOPPED:
        damper_ = false;
        if (run) {
          if (cfg_.damper_enabled) {
            damper_ = true;
            enter_(FanState::DAMPER_OPENING, now);
          } else {
            start_exhaust_(now, req);
          }
        }
        break;

      case FanState::DAMPER_OPENING:
        if (!run) {
          damper_ = false;
          enter_(FanState::STOPPED, now);
        } else if (elapsed_(now) >= cfg_.damper_open_ms) {
          start_exhaust_(now, req);
        }
        break;

      case FanState::START_EXHAUST:
        if (!run) {
          // приток ещё не запускался — нагрева не было, продувка не нужна
          exhaust_.set_target(0);
          enter_(FanState::RUNDOWN, now);
        } else {
          exhaust_.set_target(req.exhaust);
          if (exhaust_.active() > 0 && now - exhaust_running_since_ >= cfg_.exhaust_lead_ms) {
            supply_.set_target(req.supply);
            enter_(FanState::RUNNING, now);
          }
        }
        break;

      case FanState::RUNNING:
        if (!run) {
          if (supply_.target() == 0)
            supply_.set_target(1);
          enter_(FanState::PURGE, now);
        } else {
          supply_.set_target(req.supply);
          exhaust_.set_target(req.exhaust);
        }
        break;

      case FanState::PURGE:
        if (run) {
          supply_.set_target(req.supply);
          exhaust_.set_target(req.exhaust);
          enter_(FanState::RUNNING, now);
        } else if (elapsed_(now) >= cfg_.purge_ms) {
          supply_.set_target(0);
          enter_(FanState::STOP_EXHAUST, now);
        }
        break;

      case FanState::STOP_EXHAUST:
        if (elapsed_(now) >= cfg_.exhaust_lead_ms) {
          exhaust_.set_target(0);
          enter_(FanState::RUNDOWN, now);
        }
        break;

      case FanState::RUNDOWN:
        if (elapsed_(now) >= cfg_.rundown_ms) {
          damper_ = false;
          enter_(FanState::STOPPED, now);
        }
        break;
    }

    const bool supply_was_on = supply_.active() > 0;
    const bool exhaust_was_on = exhaust_.active() > 0;
    supply_.update(now);
    exhaust_.update(now);
    // момент фактического включения (фаза подана на отвод)
    if (supply_.active() > 0 && !supply_was_on)
      supply_running_since_ = now;
    if (exhaust_.active() > 0 && !exhaust_was_on)
      exhaust_running_since_ = now;
  }

  FanState state() const { return state_; }
  const char *state_text() const { return fan_state_text(state_); }
  uint32_t state_elapsed_ms(uint32_t now) const { return now - state_since_; }

  uint8_t supply_speed() const { return supply_.active(); }
  uint8_t exhaust_speed() const { return exhaust_.active(); }
  uint8_t supply_relay_mask() const { return supply_.relay_mask(); }
  uint8_t exhaust_relay_mask() const { return exhaust_.relay_mask(); }
  bool damper_open_cmd() const { return cfg_.damper_enabled && damper_; }

  // Разрешение нагрева от вентиляции: режим РАБОТА, приток реально вращается
  // без перерыва не меньше heat_delay и не в паузе переключения ступени.
  bool heat_permitted(uint32_t now) const {
    return state_ == FanState::RUNNING && supply_.active() > 0 && supply_.settled() &&
           exhaust_.active() > 0 && supply_running_ms(now) >= cfg_.heat_delay_ms;
  }

  // Время непрерывной работы притока (для контроля потока, шаг 5).
  uint32_t supply_running_ms(uint32_t now) const {
    return supply_.active() > 0 ? now - supply_running_since_ : 0;
  }

 private:
  void enter_(FanState s, uint32_t now) {
    state_ = s;
    state_since_ = now;
  }
  uint32_t elapsed_(uint32_t now) const { return now - state_since_; }
  void start_exhaust_(uint32_t now, const FanRequest &req) {
    exhaust_.set_target(req.exhaust);
    enter_(FanState::START_EXHAUST, now);
  }

  FanConfig cfg_{};
  TapSpeedDriver supply_{};
  TapSpeedDriver exhaust_{};
  FanState state_{FanState::STOPPED};
  uint32_t state_since_{0};
  uint32_t supply_running_since_{0};
  uint32_t exhaust_running_since_{0};
  bool damper_{false};
};

// Единственный экземпляр для прошивки (globals ESPHome объявляются раньше includes,
// поэтому свой тип туда не положить).
inline FanController &fan_controller() {
  static FanController instance;
  return instance;
}

}  // namespace vent
