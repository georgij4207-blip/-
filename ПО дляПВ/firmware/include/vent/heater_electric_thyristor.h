#pragma once
// Электро, тиристорный регулятор 0–10 В — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: AnalogOut 0–10 В на регулятор, DigitalOut «разрешение»; ПИ и защиты как у
// heater_electric_pwm.h.

#include "heater.h"

namespace vent {

class ElectricThyristorHeater : public HeaterStub {
 public:
  const char *type_name() const override { return "Электро, тиристорный регулятор 0–10 В"; }
};

}  // namespace vent
