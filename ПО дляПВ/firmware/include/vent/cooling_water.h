#pragma once
// Водяной охладитель — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: клапан AnalogOut 0–10 В / 3-точечный, насос DigitalOut, ПИ по t_supply,
// датчик протечки/уровня в поддоне (SignalIn) → авария.

#include "cooling.h"

namespace vent {

class WaterCooler : public CoolingStub {
 public:
  const char *type_name() const override { return "Водяной охладитель"; }
};

}  // namespace vent
