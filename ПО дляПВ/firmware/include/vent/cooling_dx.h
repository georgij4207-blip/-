#pragma once
// Фреоновый ККБ — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: DigitalOut пуск (или ступени), AnalogOut 0–10 В для инвертора, SignalIn авария ККБ;
// минимальное время работы/паузы и задержка повторного пуска; защита по t притока
// (обмерзание испарителя); дренаж (SignalIn) → авария; только при потоке.

#include "cooling.h"

namespace vent {

class DxCooler : public CoolingStub {
 public:
  const char *type_name() const override { return "Фреоновый ККБ"; }
};

}  // namespace vent
