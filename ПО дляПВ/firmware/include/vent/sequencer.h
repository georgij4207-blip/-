#pragma once
// Последовательность пуска/останова вентиляторов. Не знает, какие у установки моторы
// и заслонка: работает через FanDriver/DamperDriver. Приточного или вытяжного вентилятора
// может не быть (вытяжная/приточная установка) — тогда соответствующие шаги пропускаются.
//
// Пуск:   [заслонка открыть → ждать is_open] → вытяжка → ждать → приток → РАБОТА
//         (нагрев разрешается через heat_delay после того, как приток реально работает)
// Останов: нагрев запрещён сразу → продувка притоком → стоп притока → ждать →
//          стоп вытяжки → выбег → [заслонка закрыть]
// Только вытяжка (оттайка рекуператора): продувка → стоп притока, вытяжка работает.

#include <cstdint>

#include "damper.h"
#include "fan.h"

namespace vent {

struct FanRequest {
  uint8_t supply{0};   // 0 = стоп притока
  uint8_t exhaust{0};  // 0 = стоп вытяжки
  bool emergency_stop{false};    // немедленный стоп без продувки
  bool close_damper_now{false};  // вместе с emergency_stop: закрыть заслонку без выбега
  bool hold_purge{false};        // продувку не заканчивать (приток ещё горячий)
};

inline uint8_t clamp_level(int s, uint8_t max_level) {
  return s < 1 ? 1 : (s > max_level ? max_level : (uint8_t) s);
}

// Общий режим + смещение вытяжки. Для раздельной уставки достаточно сформировать
// FanRequest иначе — последовательность от этого не меняется.
inline FanRequest speeds_from_mode(uint8_t mode, int exhaust_offset, uint8_t max_level) {
  FanRequest r;
  if (mode == 0)
    return r;
  r.supply = clamp_level(mode, max_level);
  r.exhaust = clamp_level(mode + exhaust_offset, max_level);
  return r;
}

struct SequencerConfig {
  uint32_t exhaust_lead_ms{3000};  // вытяжка раньше притока при пуске, позже при останове
  uint32_t purge_ms{120000};
  uint32_t heat_delay_ms{10000};
  uint32_t rundown_ms{15000};      // выбег до закрытия заслонки
};

enum class FanState : uint8_t {
  STOPPED,
  DAMPER_OPENING,
  START_EXHAUST,
  RUNNING,
  PURGE,
  EXHAUST_ONLY,
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
    case FanState::EXHAUST_ONLY: return "Только вытяжка";
    case FanState::STOP_EXHAUST: return "Останов вытяжки";
    case FanState::RUNDOWN: return "Выбег";
    default: return "?";
  }
}

class FanSequencer {
 public:
  void configure(const SequencerConfig &cfg, FanDriver *supply, FanDriver *exhaust, DamperDriver *damper) {
    cfg_ = cfg;
    supply_ = supply;
    exhaust_ = exhaust;
    damper_ = damper;
  }

  void update(uint32_t now, const FanRequest &req) {
    // Установка работает, пока нужна вытяжка (или приток, если вытяжки нет).
    bool run = exhaust_ ? req.exhaust > 0 : req.supply > 0;
    run = run && !req.emergency_stop;
    const bool supply_req = run && supply_ && req.supply > 0;

    if (req.emergency_stop) {
      if (state_ != FanState::STOPPED && state_ != FanState::RUNDOWN) {
        set_supply_(0);
        set_exhaust_(0);
        enter_(FanState::RUNDOWN, now);
      }
      if (req.close_damper_now && damper_)
        damper_->command(false);
    }

    switch (state_) {
      case FanState::STOPPED:
        if (damper_)
          damper_->command(false);
        if (run) {
          if (damper_) {
            damper_->command(true);
            enter_(FanState::DAMPER_OPENING, now);
          } else {
            start_fans_(now, req, supply_req);
          }
        }
        break;

      case FanState::DAMPER_OPENING:
        if (!run) {
          damper_->command(false);
          enter_(FanState::STOPPED, now);
        } else if (damper_->is_open()) {
          start_fans_(now, req, supply_req);
        }
        break;

      case FanState::START_EXHAUST:
        if (!run) {
          // приток ещё не запускался — нагрева не было, продувка не нужна
          set_exhaust_(0);
          enter_(FanState::RUNDOWN, now);
        } else {
          set_exhaust_(req.exhaust);
          if (exhaust_->active_level() > 0 && now - exhaust_running_since_ >= cfg_.exhaust_lead_ms) {
            if (supply_req) {
              set_supply_(req.supply);
              enter_(FanState::RUNNING, now);
            } else if (supply_) {
              enter_(FanState::EXHAUST_ONLY, now);
            } else {
              enter_(FanState::RUNNING, now);  // вытяжная установка
            }
          }
        }
        break;

      case FanState::RUNNING:
        if (supply_ && !supply_req) {
          // стоп установки или переход в «только вытяжка» — сначала продувка
          if (target_supply_ == 0)
            set_supply_(1);
          enter_(FanState::PURGE, now);
        } else if (!supply_ && !run) {
          enter_(FanState::STOP_EXHAUST, now);
        } else {
          set_supply_(req.supply);
          set_exhaust_(req.exhaust);
        }
        break;

      case FanState::PURGE:
        if (supply_req) {
          set_supply_(req.supply);
          set_exhaust_(req.exhaust);
          enter_(FanState::RUNNING, now);
        } else if (now - state_since_ >= cfg_.purge_ms && !req.hold_purge) {
          set_supply_(0);
          if (!exhaust_)
            enter_(FanState::RUNDOWN, now);
          else
            enter_(run ? FanState::EXHAUST_ONLY : FanState::STOP_EXHAUST, now);
        }
        break;

      case FanState::EXHAUST_ONLY:
        if (!run) {
          enter_(FanState::STOP_EXHAUST, now);
        } else {
          set_exhaust_(req.exhaust);
          if (supply_req) {
            set_supply_(req.supply);
            enter_(FanState::RUNNING, now);
          }
        }
        break;

      case FanState::STOP_EXHAUST:
        if (now - state_since_ >= cfg_.exhaust_lead_ms) {
          set_exhaust_(0);
          enter_(FanState::RUNDOWN, now);
        }
        break;

      case FanState::RUNDOWN:
        if (now - state_since_ >= cfg_.rundown_ms) {
          if (damper_)
            damper_->command(false);
          enter_(FanState::STOPPED, now);
        }
        break;
    }

    const bool supply_was_on = supply_level() > 0;
    const bool exhaust_was_on = exhaust_level() > 0;
    if (supply_)
      supply_->update(now);
    if (exhaust_)
      exhaust_->update(now);
    // момент фактического включения
    if (supply_level() > 0 && !supply_was_on)
      supply_running_since_ = now;
    if (exhaust_level() > 0 && !exhaust_was_on)
      exhaust_running_since_ = now;
  }

  FanState state() const { return state_; }
  const char *state_text() const { return fan_state_text(state_); }
  bool in_operation() const {
    return state_ == FanState::RUNNING || state_ == FanState::PURGE || state_ == FanState::EXHAUST_ONLY;
  }

  uint8_t supply_level() const { return supply_ ? supply_->active_level() : 0; }
  uint8_t exhaust_level() const { return exhaust_ ? exhaust_->active_level() : 0; }

  // Разрешение нагрева от вентиляции: РАБОТА, приток реально вращается без перерыва
  // не меньше heat_delay и не в паузе смены ступени, вытяжка (если есть) работает.
  bool heat_permitted(uint32_t now) const {
    return state_ == FanState::RUNNING && supply_ && supply_level() > 0 && supply_->settled() &&
           (!exhaust_ || exhaust_level() > 0) && supply_running_ms(now) >= cfg_.heat_delay_ms;
  }

  uint32_t supply_running_ms(uint32_t now) const {
    return supply_level() > 0 ? now - supply_running_since_ : 0;
  }

 private:
  void enter_(FanState s, uint32_t now) {
    state_ = s;
    state_since_ = now;
  }
  void set_supply_(uint8_t l) {
    target_supply_ = l;
    if (supply_)
      supply_->set_target(l);
  }
  void set_exhaust_(uint8_t l) {
    if (exhaust_)
      exhaust_->set_target(l);
  }
  void start_fans_(uint32_t now, const FanRequest &req, bool supply_req) {
    if (exhaust_) {
      set_exhaust_(req.exhaust);
      enter_(FanState::START_EXHAUST, now);
    } else if (supply_req) {
      set_supply_(req.supply);
      enter_(FanState::RUNNING, now);  // приточная установка
    }
  }

  SequencerConfig cfg_{};
  FanDriver *supply_{nullptr};
  FanDriver *exhaust_{nullptr};
  DamperDriver *damper_{nullptr};
  FanState state_{FanState::STOPPED};
  uint32_t state_since_{0};
  uint32_t supply_running_since_{0};
  uint32_t exhaust_running_since_{0};
  uint8_t target_supply_{0};
};

}  // namespace vent
