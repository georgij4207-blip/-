#pragma once
// EC по Modbus RTU — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: профили регистров по маркам (ebm-papst, Ziehl-Abegg, Rosenberg) в отдельных
// таблицах; задание скорости, пуск/стоп, чтение оборотов, тока, кода аварии.
// Транспорт — Modbus RTU master (компонент modbus_controller ESPHome) через AnalogOut/AnalogIn
// на регистры. Потеря связи → авария.

#include "fan.h"

namespace vent {

class EcModbusFan : public FanStub {
 public:
  const char *type_name() const override { return "EC по Modbus RTU"; }
};

}  // namespace vent
