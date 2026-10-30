#pragma once
// Водяной калорифер — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: клапан (AnalogOut 0–10 В или 3-точечный — два DigitalOut), насос (DigitalOut),
// датчик обратки (AnalogIn), капиллярный термостат разморозки (SignalIn).
//  - прогрев перед пуском зимой: клапан 100 %, насос, пуск вентиляторов после
//    t_обратки > порога (start_permitted);
//  - дежурный режим при стопе: поддержание t_обратки (клапан по ПИ, насос вкл);
//  - угроза разморозки (обратка < порога или термостат): клапан 100 %, насос, стоп
//    вентиляторов, закрыть заслонку — новые коды аварий в alarms.h;
//  - летом: насос периодически прокручивать.

#include "heater.h"

namespace vent {

class WaterHeater : public HeaterStub {
 public:
  const char *type_name() const override { return "Водяной калорифер"; }
  bool needs_airflow() const override { return false; }
  bool start_permitted() const override { return false; }
};

}  // namespace vent
