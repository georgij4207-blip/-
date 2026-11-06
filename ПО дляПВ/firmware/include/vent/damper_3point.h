#pragma once
// 3-точечный привод — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: два DigitalOut (открыть/закрыть, взаимоблокировка), время хода, концевики
// (SignalIn), положение по времени хода при отсутствии концевиков.

#include "damper.h"

namespace vent {

class ThreePointDamper : public DamperStub {
 public:
  const char *type_name() const override { return "3-точечный привод"; }
};

}  // namespace vent
