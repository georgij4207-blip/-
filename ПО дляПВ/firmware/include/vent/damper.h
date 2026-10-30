#pragma once
// Интерфейс драйвера заслонки (приводы наружного/вытяжного воздуха, байпаса, рециркуляции).
// Последовательность пуска ждёт is_open(), при останове командует close.
//
// Реализации:
//   damper_spring.h   on/off с пружинным возвратом, опц. концевик   — реализован (Salda)
//   damper_3point.h   3-точечный привод (открыть/закрыть), концевики — заглушка
//   damper_analog.h   0–10 В с обратной связью 2–10 В              — заглушка
// Нет заслонки — драйвер не задаётся (nullptr).

#include <cstdint>

#include "hal.h"

namespace vent {

class DamperDriver {
 public:
  virtual ~DamperDriver() = default;
  virtual const char *type_name() const = 0;
  virtual bool implemented() const { return true; }

  virtual void command(bool open) = 0;
  virtual void update(uint32_t now_ms, AlarmSink &alarms) = 0;
  virtual bool is_open() const = 0;  // можно пускать вентиляторы
  virtual bool commanded_open() const = 0;
};

class DamperStub : public DamperDriver {
 public:
  bool implemented() const override { return false; }
  void command(bool) override {}
  void update(uint32_t, AlarmSink &) override {}
  bool is_open() const override { return false; }
  bool commanded_open() const override { return false; }
};

}  // namespace vent
