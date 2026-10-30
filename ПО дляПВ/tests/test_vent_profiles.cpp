// Хост-тест профилей: проверка конфигурации, заглушки драйверов, другие типы установок.
// Подключает ВСЕ заголовки драйверов — заодно проверка, что каждый компилируется и
// реализует свой интерфейс.
// Сборка: g++ -std=c++17 -I../firmware/include -I. test_vent_profiles.cpp -o t && ./t
#include <cassert>
#include <cstdio>
#include <cstring>

#include "salda_profile.h"
#include "vent/cooling_dx.h"
#include "vent/cooling_water.h"
#include "vent/damper_3point.h"
#include "vent/damper_analog.h"
#include "vent/fan_ac_regulator.h"
#include "vent/fan_ec_analog.h"
#include "vent/fan_ec_modbus.h"
#include "vent/fan_vfd.h"
#include "vent/heater_electric_steps.h"
#include "vent/heater_electric_thyristor.h"
#include "vent/heater_water.h"
#include "vent/recuperator_enthalpy.h"
#include "vent/recuperator_glycol.h"
#include "vent/recuperator_rotary.h"

using namespace vent;

static uint32_t sec(uint32_t s) { return s * 1000; }

static VentInputs normal_inputs() {
  VentInputs in;
  in.mode = 2;
  in.t_outdoor = -10;
  in.t_supply = 18;
  in.t_extract = 22;
  in.t_exhaust = 5;
  in.flow_ok = true;
  return in;
}

static void run(VentSystem &sys, uint32_t &now, uint32_t ms, const VentInputs &in) {
  for (uint32_t t = 0; t < ms; t += 200) {
    now += 200;
    sys.update(now, in);
  }
}

// Профиль с одной заменённой частью: система не должна запускаться.
template <typename Setup> static void expect_config_error(const char *what, Setup setup) {
  SaldaRig r;
  setup(r);
  r.sys.configure(VentConfig{});
  uint32_t now = 0;
  run(r.sys, now, sec(120), normal_inputs());
  assert(!r.sys.config_ok());
  assert(r.sys.alarms().on(AL_CONFIG));
  assert(r.sys.fans().state() == FanState::STOPPED);
  for (auto &k : r.k)
    if (&k != &r.k[7]) assert(!k.v);  // ни одно реле, кроме «нормы», не включалось
  assert(!r.k[7].v);                  // «норма» снята
  assert(r.heater_pct() == 0);
  std::printf("   %-46s → «%s»\n", what, r.sys.config_error());
}

int main() {
  // --- 1. Профиль Salda валиден ---
  {
    SaldaRig r;
    assert(r.sys.config_ok());
    char buf[512];
    r.sys.profile_text(buf, sizeof(buf));
    std::printf("1. Salda: %s\n", buf);
    // обрезка по границе символа UTF-8
    for (size_t n = 8; n < 200; n++) {
      char small[200];
      r.sys.profile_text(small, n);
      const size_t len = strlen(small);
      assert(len < n);
      int i = (int) len - 1;
      while (i > 0 && ((unsigned char) small[i] & 0xC0) == 0x80) i--;
      const unsigned char lead = (unsigned char) small[i];
      const size_t need = lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
      assert(len - i == need);  // последний символ целый
    }
  }

  // --- 2. Заглушки и несовместимые наборы: авария конфигурации, установка стоит ---
  std::printf("2. заглушки и ошибки профиля:\n");
  EcAnalogFan ec;
  EcModbusFan ecm;
  VfdFan vfd;
  AcRegulatorFan acr;
  ElectricStepsHeater steps;
  ElectricThyristorHeater thyr;
  WaterHeater water;
  RotaryRecuperator rotary;
  EnthalpyRecuperator enth;
  GlycolRecuperator glycol;
  ThreePointDamper d3;
  AnalogDamper da;
  WaterCooler wc;
  DxCooler dx;
  expect_config_error("приток: EC 0–10 В", [&](SaldaRig &r) { r.sys.set_supply_fan(&ec); });
  expect_config_error("вытяжка: EC Modbus", [&](SaldaRig &r) { r.sys.set_exhaust_fan(&ecm); });
  expect_config_error("приток: частотник", [&](SaldaRig &r) { r.sys.set_supply_fan(&vfd); });
  expect_config_error("приток: AC через регулятор", [&](SaldaRig &r) { r.sys.set_supply_fan(&acr); });
  expect_config_error("нагрев: ступени", [&](SaldaRig &r) { r.sys.set_heater(&steps); });
  expect_config_error("нагрев: тиристор", [&](SaldaRig &r) { r.sys.set_heater(&thyr); });
  expect_config_error("нагрев: водяной", [&](SaldaRig &r) { r.sys.set_heater(&water); });
  expect_config_error("рекуператор: роторный", [&](SaldaRig &r) { r.sys.set_recuperator(&rotary); });
  expect_config_error("рекуператор: энтальпийный", [&](SaldaRig &r) { r.sys.set_recuperator(&enth); });
  expect_config_error("рекуператор: гликолевый", [&](SaldaRig &r) { r.sys.set_recuperator(&glycol); });
  expect_config_error("заслонка: 3-точечная", [&](SaldaRig &r) { r.sys.set_damper(&d3); });
  expect_config_error("заслонка: 0–10 В", [&](SaldaRig &r) { r.sys.set_damper(&da); });
  expect_config_error("охлаждение: водяное", [&](SaldaRig &r) { r.sys.set_cooling(&wc); });
  expect_config_error("охлаждение: ККБ", [&](SaldaRig &r) { r.sys.set_cooling(&dx); });
  expect_config_error("приточно-вытяжная без вытяжки", [&](SaldaRig &r) { r.sys.set_exhaust_fan(nullptr); });
  {
    SaldaRig r;  // пять ступеней в режиме, а у моторов три
    VentConfig c;
    c.fan_levels = 5;
    r.sys.configure(c);
    assert(!r.sys.config_ok());
    std::printf("   %-46s → «%s»\n", "режим на 5 ступеней, моторы на 3", r.sys.config_error());
  }

  // --- 3. Приточная установка: AC-мотор + ТЭН, без рекуператора и вытяжки ---
  {
    fake::Out k[4];
    fake::AOut ssr;
    fake::Sig th;
    fake::Num kp(10), ti(120);
    DigitalOut *taps[3] = {&k[0], &k[1], &k[2]};
    AcTapsFan fan(taps, 3, 1500);
    ElectricPwmHeater heater(&ssr, &kp, &ti, &th, nullptr, ElectricPwmConfig{});
    VentSystem sys;
    sys.set_supply_fan(&fan);
    sys.set_heater(&heater);
    sys.set_alarm_output(&k[3]);
    VentConfig c;
    c.unit_type = UnitType::SUPPLY;
    sys.configure(c);
    assert(sys.config_ok());
    VentInputs in = normal_inputs();
    in.t_supply = 12;
    in.t_exhaust = NAN;  // датчиков вытяжной части нет — и не требуются
    in.t_extract = NAN;
    uint32_t now = 0;
    run(sys, now, sec(60), in);
    assert(sys.fans().state() == FanState::RUNNING && fan.active_level() == 2);
    assert(ssr.v > 0 && k[3].v);  // греет, «норма»
    assert(!sys.alarms().on(AL_SENSOR_EXHAUST) && !sys.alarms().on(AL_SENSOR_EXTRACT));
    in.mode = 0;
    run(sys, now, sec(1), in);
    assert(ssr.v == 0 && sys.fans().state() == FanState::PURGE);
    std::printf("3. приточная (AC + ТЭН): пуск, нагрев, продувка — OK\n");
  }

  // --- 4. Вытяжная установка: только вытяжной вентилятор, датчики не нужны ---
  {
    fake::Out k[3];
    DigitalOut *taps[3] = {&k[0], &k[1], &k[2]};
    AcTapsFan fan(taps, 3, 1500);
    VentSystem sys;
    sys.set_exhaust_fan(&fan);
    VentConfig c;
    c.unit_type = UnitType::EXHAUST;
    sys.configure(c);
    assert(sys.config_ok());
    VentInputs in;
    in.mode = 3;
    uint32_t now = 0;
    run(sys, now, sec(30), in);
    assert(sys.fans().state() == FanState::RUNNING && fan.active_level() == 3);
    assert(sys.status().ok);
    std::printf("4. вытяжная: работает без датчиков температуры — OK\n");
  }

  // --- 5. Приточно-вытяжная без рекуператора (рекуператор не задан) ---
  {
    SaldaRig r;
    r.sys.set_recuperator(nullptr);
    r.sys.configure(VentConfig{});
    assert(r.sys.config_ok());
    VentInputs in = normal_inputs();
    in.t_exhaust = NAN;  // без рекуператора датчик выброса не обязателен
    uint32_t now = 0;
    run(r.sys, now, sec(60), in);
    assert(r.sys.fans().state() == FanState::RUNNING && !r.sys.alarms().on(AL_SENSOR_EXHAUST));
    std::printf("5. приточно-вытяжная без рекуператора — OK\n");
  }

  std::printf("OK\n");
  return 0;
}
