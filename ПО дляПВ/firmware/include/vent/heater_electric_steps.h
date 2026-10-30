#pragma once
// Электро, ступени контакторами — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: N DigitalOut на контакторы, ПИ → число ступеней с гистерезисом и минимальным
// временем между переключениями (ресурс контакторов); термостаты как у heater_electric_pwm.h.

#include "heater.h"

namespace vent {

class ElectricStepsHeater : public HeaterStub {
 public:
  const char *type_name() const override { return "Электро, ступени контакторами"; }
};

}  // namespace vent
