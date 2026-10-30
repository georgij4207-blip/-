#pragma once
// Энтальпийный мембранный рекуператор — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: как пластинчатый (recuperator_plate.h), но порог обмерзания ниже (мембрана
// переносит влагу), ограничение по влажности вытяжки.

#include "recuperator.h"

namespace vent {

class EnthalpyRecuperator : public RecuperatorStub {
 public:
  const char *type_name() const override { return "Энтальпийный мембранный"; }
};

}  // namespace vent
