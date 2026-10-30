#pragma once
// Интерфейс драйвера рекуператора и его защиты от обмерзания.
// adjust() может менять скорости (смещение баланса, «только вытяжка»), update() — управлять
// своими выходами (преднагрев, байпас, ротор, насос гликоля) и сообщать аварии.
//
// Реализации:
//   recuperator_none.h       рекуператора нет (приточная/вытяжная)                — реализован
//   recuperator_plate.h      пластинчатый перекрёстный/противоточный: преднагрев,
//                            снижение притока/усиление вытяжки, только вытяжка    — реализован (Salda)
//   recuperator_rotary.h     роторный: скорость ротора, контроль вращения         — заглушка
//   recuperator_enthalpy.h   энтальпийный мембранный                               — заглушка
//   recuperator_glycol.h     гликолевый: насос, клапан                             — заглушка

#include <cmath>
#include <cstdint>

#include "hal.h"
#include "sequencer.h"

namespace vent {

struct RecupContext {
  uint32_t now_ms{0};
  bool allowed{false};       // установка должна работать (нет стопа/пожара/охлаждения, режим > 0)
  bool in_operation{false};  // вентиляторы в работе (работа, продувка, только вытяжка)
  bool airflow_ok{false};    // приток вращается и поток подтверждён
  FanRequest base{};         // скорости по режиму
  float t_outdoor{NAN}, t_extract{NAN}, t_exhaust{NAN};
};

class RecuperatorDriver {
 public:
  virtual ~RecuperatorDriver() = default;
  virtual const char *type_name() const = 0;
  virtual bool implemented() const { return true; }
  virtual bool needs_exhaust_sensor() const { return false; }

  virtual FanRequest adjust(const FanRequest &base) const { return base; }
  virtual void update(const RecupContext &ctx, AlarmSink &alarms) = 0;
  virtual bool preheater_active() const { return false; }
  virtual int frost_level() const { return 0; }
};

class RecuperatorStub : public RecuperatorDriver {
 public:
  bool implemented() const override { return false; }
  void update(const RecupContext &, AlarmSink &) override {}
};

}  // namespace vent
