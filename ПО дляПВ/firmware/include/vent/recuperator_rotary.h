#pragma once
// Роторный рекуператор — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: скорость ротора — AnalogOut 0–10 В или частотник (через fan_vfd.h-подобный выход),
// контроль вращения — датчик оборотов (SignalIn импульсы) → авария «ротор стоит»;
// защита от обмерзания — снижение скорости ротора; летом — ротор стоп/по разности температур.

#include "recuperator.h"

namespace vent {

class RotaryRecuperator : public RecuperatorStub {
 public:
  const char *type_name() const override { return "Роторный"; }
};

}  // namespace vent
