#pragma once
// Гликолевый рекуператор — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: насос (DigitalOut), трёхходовой клапан (AnalogOut) — защита от обмерзания
// калорифера-рекуператора на вытяжке по t после него; контроль давления/потока гликоля.

#include "recuperator.h"

namespace vent {

class GlycolRecuperator : public RecuperatorStub {
 public:
  const char *type_name() const override { return "Гликолевый"; }
};

}  // namespace vent
