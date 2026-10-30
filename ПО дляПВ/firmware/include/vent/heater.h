#pragma once
// Интерфейс драйвера основного нагревателя притока.
// Система решает «можно ли греть» (поток, аварии, зима) и даёт уставку и температуры;
// как греть (ПИ на SSR, ступени, клапан) и свои защиты — дело драйвера.
//
// Реализации:
//   heater_electric_pwm.h        электро, SSR с ШИМ, термостаты +50/+100 °C — реализован (Salda)
//   heater_electric_steps.h      электро, ступени контакторами               — заглушка
//   heater_electric_thyristor.h  электро, тиристорный регулятор 0–10 В       — заглушка
//   heater_water.h               водяной калорифер (клапан, насос, обратка)  — заглушка
// Нет нагревателя — драйвер не задаётся (nullptr).

#include <cmath>
#include <cstdint>

#include "hal.h"

namespace vent {

struct HeatContext {
  uint32_t now_ms{0};
  float dt_s{0.0f};
  bool heat_allowed{false};    // нет стоповых аварий, «Нагрев (зима)» включён
  bool airflow_ok{false};      // приток вращается, поток подтверждён, не в паузе смены ступени
  bool unit_running{false};    // установка в работе (водяной: иначе дежурный режим)
  uint32_t supply_running_ms{0};
  bool preheat_active{false};  // преднагрев рекуператора включён (тоже даёт тепло)
  float setpoint{NAN};
  float t_supply{NAN}, t_outdoor{NAN}, t_extract{NAN};
};

class HeaterDriver {
 public:
  virtual ~HeaterDriver() = default;
  virtual const char *type_name() const = 0;
  virtual bool implemented() const { return true; }

  // Электро: греть только при потоке. Водяной: false (клапан держит калорифер тёплым и без потока).
  virtual bool needs_airflow() const = 0;
  // Можно ли пускать вентиляторы (водяной: прогрев калорифера перед пуском зимой).
  virtual bool start_permitted() const { return true; }
  virtual void update(const HeatContext &ctx, AlarmSink &alarms) = 0;
  virtual float output_pct() const = 0;  // для интерфейса и журнала
};

class HeaterStub : public HeaterDriver {
 public:
  bool implemented() const override { return false; }
  bool needs_airflow() const override { return true; }
  void update(const HeatContext &, AlarmSink &) override {}
  float output_pct() const override { return 0.0f; }
};

}  // namespace vent
