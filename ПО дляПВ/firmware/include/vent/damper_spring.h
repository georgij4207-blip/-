#pragma once
// Привод с пружинным возвратом: питание подано — открывается, снято — пружина закрывает.
// Открытой считается через open_ms после команды или по концевику (если задан).
// С концевиком: не открылась за 2 × open_ms → авария «Заслонка не открылась».

#include "alarms.h"
#include "damper.h"

namespace vent {

class SpringDamper : public DamperDriver {
 public:
  SpringDamper(DigitalOut *out, uint32_t open_ms, SignalIn *open_switch = nullptr)
      : out_(out), open_ms_(open_ms), open_switch_(open_switch) {}

  const char *type_name() const override { return "Привод вкл/выкл с пружинным возвратом"; }

  void command(bool open) override {
    if (open && !cmd_)
      since_ = now_;  // отсчёт времени открытия — с момента команды (now_ — текущий цикл)
    cmd_ = open;
    out_->set(open);  // закрытие — сразу (пожар), открытие — тоже сразу
  }

  // Вызывается в начале каждого цикла, до последовательности пуска.
  void update(uint32_t now, AlarmSink &alarms) override {
    now_ = now;
    out_->set(cmd_);
    const bool late = open_switch_ && cmd_ && !open_switch_->ok() && now - since_ >= 2 * open_ms_;
    alarms.set(AL_DAMPER, late);
  }

  bool is_open() const override {
    if (!cmd_)
      return false;
    if (open_switch_)
      return open_switch_->ok();
    return now_ - since_ >= open_ms_;
  }
  bool commanded_open() const override { return cmd_; }

 private:
  DigitalOut *out_;
  uint32_t open_ms_;
  SignalIn *open_switch_;
  bool cmd_{false};
  uint32_t since_{0};
  uint32_t now_{0};
};

}  // namespace vent
