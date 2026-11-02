// Хост-тест шаблона Б универсальной платформы: EC-вентиляторы 0–10 В (vent/fan_ec_analog.h)
// и электронагрев ступенями на контакторах + плавная ступень (vent/heater_electric_steps.h).
// Сборка: g++ -std=c++17 -I../firmware/include -I. test_template_b.cpp -o t && ./t
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>

#include "salda_profile.h"
#include "vent/fan_ec_analog.h"
#include "vent/heater_electric_steps.h"

using namespace vent;

struct Sink : AlarmSink {
  uint32_t mask{0};
  void set(uint8_t code, bool c) override {
    if (c) mask |= 1u << code; else mask &= ~(1u << code);
  }
  bool on(uint8_t code) const { return mask & (1u << code); }
};

static bool near(float a, float b, float eps = 0.01f) { return std::fabs(a - b) < eps; }

int main() {
  // 1. EC: плавный разгон до уставки ступени, «разрешение», стоп сразу
  {
    fake::AOut v;
    fake::Out en;
    EcAnalogFanConfig c;  // 40 / 65 / 90 %, 10 %/с
    EcAnalogFan f(&v, &en, nullptr, nullptr, AL_FAN_SUPPLY, c);
    uint32_t t = 0;
    f.update(t);
    f.set_target(2);
    for (int i = 0; i < 30; i++) f.update(t += 100);  // 3 с
    assert(en.v && near(f.output_pct(), 30.0f, 0.5f) && !f.settled() && f.active_level() == 2);
    for (int i = 0; i < 40; i++) f.update(t += 100);  // ещё 4 с
    assert(near(f.output_pct(), 65.0f) && near(v.v, 0.65f) && f.settled());
    f.set_target(1);
    f.update(t += 100);
    assert(f.output_pct() < 65.0f && !f.settled());  // вниз — тоже плавно
    for (int i = 0; i < 30; i++) f.update(t += 100);
    assert(near(f.output_pct(), 40.0f) && f.settled());
    f.set_target(0);
    f.update(t += 100);
    assert(v.v == 0.0f && !en.v && f.active_level() == 0 && f.settled());
    std::printf("1. EC: разгон, смена ступени, стоп — OK\n");
  }

  // 2. EC: авария по входу (дольше fault_ms) и по тахо (мало оборотов на уставке)
  {
    fake::AOut v;
    fake::Sig fault;
    fake::Num rpm(1200);
    EcAnalogFanConfig c;
    c.tacho_check_ms = 10000;
    EcAnalogFan f(&v, nullptr, &fault, &rpm, AL_FAN_EXHAUST, c);
    Sink s;
    uint32_t t = 0;
    f.set_target(1);
    for (int i = 0; i < 300; i++) { f.update(t += 100); f.report(s); }
    assert(!s.on(AL_FAN_EXHAUST));
    fault.v = false;  // авария EC на 1 с — не авария
    for (int i = 0; i < 10; i++) { f.update(t += 100); f.report(s); }
    assert(!s.on(AL_FAN_EXHAUST));
    fault.v = true;
    f.update(t += 100); f.report(s);
    fault.v = false;  // 3 с — авария
    for (int i = 0; i < 30; i++) { f.update(t += 100); f.report(s); }
    assert(s.on(AL_FAN_EXHAUST));
    fault.v = true;
    rpm.v = 100;  // мотор не крутится
    for (int i = 0; i < 50; i++) { f.update(t += 100); f.report(s); }
    assert(s.on(AL_FAN_EXHAUST));
    rpm.v = 1200;
    f.update(t += 100); f.report(s);
    assert(!s.on(AL_FAN_EXHAUST));
    std::printf("2. EC: авария по входу и по тахо — OK\n");
  }

  // 3. Ступени + плавная: набор мощности, выдержка между переключениями, сброс сразу
  {
    fake::Out k[3];
    DigitalOut *ks[3] = {&k[0], &k[1], &k[2]};
    fake::AOut an;
    fake::Num kp(10), ti(120);
    fake::Sig th100, th50;
    ElectricStepsConfig cfg;  // 3 ступени + плавная (1 ступень), гистерезис 0,15, 30 с
    ElectricStepsHeater h(ks, 3, &an, &kp, &ti, &th100, &th50, cfg);
    Sink s;
    HeatContext c;
    c.heat_allowed = c.airflow_ok = true;
    c.supply_running_ms = 100000;
    c.setpoint = 30;
    c.t_supply = 0;  // далеко до уставки → 100 %
    c.t_outdoor = -20;
    c.t_extract = 22;
    c.dt_s = 1;
    int switches = 0;
    uint8_t prev = 0;
    uint32_t last = 0;
    for (uint32_t t = 1000; t <= 120000; t += 1000) {
      c.now_ms = t;
      h.update(c, s);
      if (h.steps_on() != prev) { switches++; prev = h.steps_on(); last = t; }
      // контакторы включаются по порядку
      for (int i = 0; i < 3; i++) assert(k[i].v == (i < h.steps_on()));
      if (t == 1000) assert(h.steps_on() == 1 && near(an.v, 1.0f));  // первая ступень сразу
      if (t == 20000) assert(h.steps_on() == 1);                       // не раньше 30 с
    }
    assert(h.steps_on() == 3 && near(an.v, 1.0f) && near(h.output_pct(), 100.0f) && switches == 3);
    std::printf("   набор: 1 → 3 ступени за 60 с, плавная на 100 %% — OK\n");
    // перегрели: мощность снижается — сначала плавная, потом по одной ступени не чаще 30 с
    c.t_supply = 35;
    c.now_ms = 120000;
    prev = h.steps_on();
    for (int i = 0; i < 900; i++) {
      c.now_ms += 1000;
      h.update(c, s);
      if (h.steps_on() != prev) {
        assert(h.steps_on() == prev - 1 && c.now_ms - last >= 30000);
        prev = h.steps_on();
        last = c.now_ms;
      }
    }
    assert(h.steps_on() == 0 && an.v == 0.0f);
    std::printf("   сброс: по одной ступени, не чаще 30 с — OK\n");
    // запрет (нет потока) — всё выключено сразу, без выдержки
    c.airflow_ok = false;
    c.now_ms += 1000;
    h.update(c, s);
    assert(h.steps_on() == 0 && !k[0].v && an.v == 0.0f && h.output_pct() == 0.0f);
    // термостат +100 °C — сразу и авария
    c.airflow_ok = true;
    c.t_supply = 0;
    c.now_ms += 60000;
    h.update(c, s);
    assert(h.steps_on() == 1);
    th100.v = false;
    c.now_ms += 1000;
    h.update(c, s);
    assert(h.steps_on() == 0 && s.on(AL_THERMOSTAT));
    std::printf("3. электро ступенями: набор, выдержка, сброс сразу, термостат — OK\n");
  }

  // 4. Система шаблона Б: EC приток/вытяжка + 3 ступени по 4 кВт + плавная 4 кВт, 2000 м³/ч.
  //    Пуск, выход на уставку, мало переключений контакторов, авария EC — нагрев сразу 0.
  {
    fake::AOut vs, ve;
    fake::Out ens, ene, k[3], k8;
    fake::Sig fs, fe, th100;
    fake::Num kp(10), ti(120);
    fake::AOut an;
    EcAnalogFanConfig fc;
    EcAnalogFan sup(&vs, &ens, &fs, nullptr, AL_FAN_SUPPLY, fc);
    EcAnalogFan exh(&ve, &ene, &fe, nullptr, AL_FAN_EXHAUST, fc);
    DigitalOut *ks[3] = {&k[0], &k[1], &k[2]};
    ElectricStepsHeater heat(ks, 3, &an, &kp, &ti, &th100, nullptr, ElectricStepsConfig{});
    VentSystem sys;
    sys.set_supply_fan(&sup);
    sys.set_exhaust_fan(&exh);
    sys.set_heater(&heat);
    sys.set_alarm_output(&k8);
    VentConfig cfg;
    cfg.sensor_fault_ms = 5000;
    cfg.flow_loss_ms = 3000;
    sys.configure(cfg);
    assert(sys.config_ok());

    VentInputs in;
    in.mode = 2;
    in.setpoint = 20;
    in.t_outdoor = -25;
    in.t_extract = 22;
    in.t_exhaust = 5;
    const float t_after = 2.0f;    // после рекуператора
    const float rise_full = 24.0f; // 16 кВт на 2000 м³/ч (уровень 3, 90 %) ≈ 24 К
    float t_supply = t_after;
    uint32_t now = 0;
    int switches = 0;
    uint8_t prev = 0;
    for (; now < 90u * 60000u; now += 200) {
      const float flow = vs.v / 0.9f;  // доля номинального расхода
      const float rise = flow > 0.05f ? rise_full * heat.output_pct() / 100.0f / flow : 0.0f;
      t_supply += (t_after + rise - t_supply) * 0.2f / 60.0f;
      in.t_supply = t_supply;
      in.flow_ok = sup.output_pct() > 20.0f;
      const VentStatus &o = sys.update(now, in);
      if (o.heater_pct > 0) {  // инварианты: нагрев только при работе, потоке, на уставке притока
        assert(sys.fans().state() == FanState::RUNNING && in.flow_ok && sup.settled());
      }
      assert(o.ok == k8.v);
      if (now > 30u * 60000u && heat.steps_on() != prev) switches++;
      prev = heat.steps_on();
    }
    assert(std::fabs(t_supply - 20.0f) < 1.0f);
    assert(switches <= 6);  // за последние 60 мин
    std::printf("4. система: приток %.1f °C при уставке 20, ступеней %u, плавная %.0f %%, переключений за 60 мин: %d — OK\n",
                t_supply, heat.steps_on(), an.v * 100.0f, switches);
    fs.v = false;  // авария EC притока
    for (int i = 0; i < 5; i++) sys.update(now += 200, in);
    assert(heat.output_pct() > 0);  // 1 с < fault_ms 2 с — ещё работает
    for (int i = 0; i < 10; i++) sys.update(now += 200, in);
    assert(sys.alarms().on(AL_FAN_SUPPLY) && heat.output_pct() == 0 && !k[0].v && !k8.v);
    std::printf("   авария EC притока: нагрев 0 сразу, «норма» снята, ручной сброс — OK\n");
  }
  std::printf("OK\n");
  return 0;
}
