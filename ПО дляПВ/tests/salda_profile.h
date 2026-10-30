#pragma once
// Профиль Salda RIS 400P для хост-тестов и симуляции: те же драйверы, что в прошивке
// (profiles/salda_ris400p.yaml), но каналы ввода-вывода — подделки вместо реле и SSR.

#include <memory>

#include "vent/damper_spring.h"
#include "vent/fan_ac_taps.h"
#include "vent/heater_electric_pwm.h"
#include "vent/recuperator_plate.h"
#include "vent/system.h"

namespace vent {
namespace fake {

struct Out : DigitalOut {
  bool v{false};
  void set(bool on) override { v = on; }
  bool get() const override { return v; }
};
struct AOut : AnalogOut {
  float v{0.0f};
  void set(float f) override { v = f; }
  float get() const override { return v; }
};
struct Sig : SignalIn {
  bool v{true};
  bool ok() const override { return v; }
};
struct Num : AnalogIn {
  float v;
  explicit Num(float x) : v(x) {}
  float value() const override { return v; }
};
struct NullSink : AlarmSink {
  void set(uint8_t, bool) override {}
};

}  // namespace fake

struct SaldaOptions {
  VentConfig sys{};
  PlateRecuperatorConfig recup{};
  ElectricPwmConfig heater{};
  bool damper{true};
  uint32_t damper_open_ms{30000};
  uint32_t fan_pause_ms{1500};
};

struct SaldaRig {
  fake::Out k[8];          // K1–K3 приток, K4–K6 вытяжка, K7 заслонка, K8 «норма»
  fake::AOut ssr_heater;   // SSR ТЭНа (доля 0…1)
  fake::Out ssr_preheater;
  fake::Sig th100, th50, preheat_th;
  fake::Num kp{10.0f}, ti{120.0f};

  std::unique_ptr<AcTapsFan> supply_fan, exhaust_fan;
  std::unique_ptr<ElectricPwmHeater> heater;
  std::unique_ptr<PlateRecuperator> recup;
  std::unique_ptr<SpringDamper> damper;
  VentSystem sys;

  explicit SaldaRig(const SaldaOptions &o = SaldaOptions{}) {
    DigitalOut *sup[3] = {&k[0], &k[1], &k[2]};
    DigitalOut *exh[3] = {&k[3], &k[4], &k[5]};
    supply_fan.reset(new AcTapsFan(sup, 3, o.fan_pause_ms));
    exhaust_fan.reset(new AcTapsFan(exh, 3, o.fan_pause_ms));
    heater.reset(new ElectricPwmHeater(&ssr_heater, &kp, &ti, &th100, &th50, o.heater));
    recup.reset(new PlateRecuperator(&ssr_preheater, &preheat_th, o.recup));
    sys.set_supply_fan(supply_fan.get());
    sys.set_exhaust_fan(exhaust_fan.get());
    sys.set_heater(heater.get());
    sys.set_recuperator(recup.get());
    if (o.damper) {
      damper.reset(new SpringDamper(&k[6], o.damper_open_ms));
      sys.set_damper(damper.get());
    }
    sys.set_alarm_output(&k[7]);
    sys.configure(o.sys);
  }

  uint8_t supply_mask() const { return (uint8_t) (k[0].v | k[1].v << 1 | k[2].v << 2); }
  uint8_t exhaust_mask() const { return (uint8_t) (k[3].v | k[4].v << 1 | k[5].v << 2); }
  float heater_pct() const { return ssr_heater.v * 100.0f; }
};

}  // namespace vent
