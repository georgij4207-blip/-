#pragma once
// Привод 0–10 В — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: AnalogOut 0–10 В (положение), AnalogIn 2–10 В обратной связи; «открыта» —
// обратная связь > порога; применимо к байпасу рекуператора и рециркуляции.

#include "damper.h"

namespace vent {

class AnalogDamper : public DamperStub {
 public:
  const char *type_name() const override { return "Привод 0–10 В"; }
};

}  // namespace vent
