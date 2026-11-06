#pragma once
// Интерфейс драйвера охлаждения (режим «лето»).
// Реализации:
//   cooling_water.h   водяной охладитель: клапан, насос             — заглушка
//   cooling_dx.h      фреоновый ККБ: пуск/ступени или 0–10 В инвертор,
//                     авария ККБ, задержка повторного пуска,
//                     защита по t притока, дренаж/протечка          — заглушка
// Нет охлаждения — драйвер не задаётся (nullptr).

#include <cmath>
#include <cstdint>

#include "hal.h"

namespace vent {

struct CoolContext {
  uint32_t now_ms{0};
  float dt_s{0.0f};
  bool cool_allowed{false};  // режим «лето», нет стоповых аварий
  bool airflow_ok{false};
  float setpoint{NAN};
  float t_supply{NAN}, t_outdoor{NAN}, t_extract{NAN};
};

class CoolingDriver {
 public:
  virtual ~CoolingDriver() = default;
  virtual const char *type_name() const = 0;
  virtual bool implemented() const { return true; }
  virtual void update(const CoolContext &ctx, AlarmSink &alarms) = 0;
  virtual float output_pct() const = 0;
};

class CoolingStub : public CoolingDriver {
 public:
  bool implemented() const override { return false; }
  void update(const CoolContext &, AlarmSink &) override {}
  float output_pct() const override { return 0.0f; }
};

}  // namespace vent
