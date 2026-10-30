#pragma once
// AC через регулятор (трансформаторный ступенчатый / симисторный 0–10 В) — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: ступенчатый трансформатор — как fan_ac_taps.h (реле на отводы трансформатора);
// симисторный — AnalogOut 0–10 В + DigitalOut «пуск», ступень → % по таблице профиля,
// минимальное напряжение трогания. Контроль: ток (токовый трансформатор, AnalogIn),
// термоконтакт обмотки (SignalIn).

#include "fan.h"

namespace vent {

class AcRegulatorFan : public FanStub {
 public:
  const char *type_name() const override { return "AC через регулятор (трансформаторный ступенчатый / симисторный 0–10 В)"; }
};

}  // namespace vent
