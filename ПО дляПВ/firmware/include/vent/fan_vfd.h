#pragma once
// Частотный преобразователь — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: вариант A — DigitalOut «пуск» + AnalogOut 0–10 В / 4–20 мА + SignalIn «авария ПЧ»;
// вариант B — Modbus RTU, профили регистров: Delta, INVT, Veda, Овен ПЧВ, Danfoss, Schneider.
// Ступень → частота по таблице профиля, время разгона/торможения — параметры ПЧ.

#include "fan.h"

namespace vent {

class VfdFan : public FanStub {
 public:
  const char *type_name() const override { return "Частотный преобразователь"; }
};

}  // namespace vent
