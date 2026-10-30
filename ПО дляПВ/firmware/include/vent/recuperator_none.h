#pragma once
// Рекуператора нет: скорости не меняются, защит нет.

#include "recuperator.h"

namespace vent {

class NoRecuperator : public RecuperatorDriver {
 public:
  const char *type_name() const override { return "Нет"; }
  void update(const RecupContext &, AlarmSink &) override {}
};

}  // namespace vent
