// Хост-тест системы (vent/system.h) с драйверами профиля Salda: нагрев и защиты.
// Упрощённая модель: приток = температура после рекуператора + ТЭН (до +11 °C при 100 %),
// инерция ТЭНа/канала — первый порядок, τ = 60 с. Полная модель — на шаге 6 (Python).
// Сборка: g++ -std=c++17 -I../firmware/include -I. test_vent_system.cpp -o t && ./t
#include <cassert>
#include <cstdio>
#include <cstring>
#include "salda_profile.h"

using namespace vent;

static const uint32_t TICK = 200;

struct Plant {
  float t_after_recup = 10.0f;  // °C перед ТЭНом
  float t_supply = 10.0f;
  float max_rise = 11.0f;       // 1,5 кВт на 400 м³/ч
  bool ssr_welded = false;      // ТЭН греет всегда, независимо от команды
  void step(float heater_pct, bool supply_on, float dt) {
    const float duty = ssr_welded ? 1.0f : heater_pct / 100.0f;
    const float target = supply_on ? t_after_recup + max_rise * duty : t_supply;
    t_supply += (target - t_supply) * dt / 60.0f;
  }
};

struct Rig {
  SaldaRig salda;
  VentSystem &sys;
  VentInputs in;
  Plant plant;
  uint32_t now = 0;
  bool override_supply = false;
  bool auto_flow = true;  // реле давления замкнуто, когда работает приток
  float forced_supply = 0;
  float max_heat = 0, max_t_supply = -100;

  explicit Rig(const SaldaOptions &o = SaldaOptions{}) : salda(o), sys(salda.sys) {
    in.mode = 2;
    in.t_outdoor = -10;
    in.t_extract = 22;
    in.t_exhaust = 5;
    in.flow_ok = true;
  }
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += TICK) {
      now += TICK;
      const bool sup = sys.fans().supply_level() > 0;
      plant.step(salda.heater_pct(), sup, TICK / 1000.0f);
      if (auto_flow)
        in.flow_ok = sup;
      if (!override_supply)
        in.t_supply = plant.t_supply;
      else
        in.t_supply = forced_supply;
      const VentStatus &o = sys.update(now, in);
      assert(std::fabs(o.heater_pct - salda.heater_pct()) < 1e-3f);  // статус = выход SSR
      assert(o.ok == salda.k[7].v);                 // K8 «норма»
      // Инварианты безопасности на каждом шаге
      if (o.heater_pct > 0 || o.preheater) {
        assert(sys.fans().supply_level() > 0);
        assert(sys.fans().state() == FanState::RUNNING);
        assert(in.flow_ok);
      }
      if (o.heater_pct > max_heat) max_heat = o.heater_pct;
      if (!override_supply && in.t_supply > max_t_supply && sys.fans().state() == FanState::RUNNING)
        max_t_supply = in.t_supply;
    }
  }
  bool stopped() const { return sys.fans().state() == FanState::STOPPED; }
};

static uint32_t sec(uint32_t s) { return s * 1000; }

int main() {
  // 1. Нормальный пуск, −10 °C, после рекуператора +10 °C: выход на уставку 18 °C
  {
    Rig r;
    r.run(sec(15 * 60));
    assert(std::fabs(r.in.t_supply - 18.0f) < 0.5f);
    assert(r.sys.status().heater_pct > 60 && r.sys.status().heater_pct < 85);
    assert(r.max_t_supply < 19.5f);  // перерегулирование < 1,5 °C
    assert(r.sys.status().ok);
    std::printf("1. уставка: приток %.1f °C, ТЭН %.0f %%, макс. %.1f °C\n", r.in.t_supply,
                r.sys.status().heater_pct, r.max_t_supply);
  }

  // 2. Мощности не хватает (после рекуператора +3 °C → максимум 14 °C):
  //    ТЭН 100 %, интегратор не выше 100 %, через 15 мин предупреждение;
  //    потом потеплело — перерегулирование небольшое (anti-windup)
  {
    Rig r;
    r.plant.t_after_recup = 3;
    r.run(sec(20 * 60));
    assert(r.sys.status().heater_pct > 99);
    assert(r.salda.heater->pi().integral() <= 100.0f);
    assert(r.sys.alarms().on(AL_HEAT_INSUFFICIENT));
    assert(r.sys.status().ok);  // предупреждение, не авария
    r.plant.t_after_recup = 12;
    r.max_t_supply = -100;
    r.run(sec(20 * 60));
    assert(!r.sys.alarms().on(AL_HEAT_INSUFFICIENT));
    assert(std::fabs(r.in.t_supply - 18.0f) < 0.5f);
    assert(r.max_t_supply < 20.0f);
    std::printf("2. нехватка мощности: предупреждение есть; после потепления макс. %.1f °C\n",
                r.max_t_supply);
  }

  // 3. Перегрев притока: нагрев сразу 0, продувка не заканчивается, пока приток горячий;
  //    горячо при выключенном нагреве дольше 3 мин → «нагрев не управляется», вентиляторы на 3
  {
    Rig r;
    r.run(sec(5 * 60));
    r.override_supply = true;
    r.forced_supply = 50;
    r.run(TICK);
    assert(r.sys.alarms().on(AL_OVERHEAT));
    assert(r.sys.status().heater_pct == 0 && !r.sys.status().ok);
    assert(r.sys.fans().state() == FanState::PURGE);
    r.run(sec(150));
    assert(r.sys.fans().state() == FanState::PURGE);  // 120 с прошли, но приток > 35 °C
    r.run(sec(60));
    assert(r.sys.alarms().on(AL_HEAT_UNCONTROLLED));
    r.run(sec(10));
    assert(r.sys.fans().supply_level() == 3 && r.sys.fans().exhaust_level() == 3);
    r.sys.reset_alarms(r.now, 0);
    assert(r.sys.alarms().on(AL_OVERHEAT));  // причина ещё есть — не сбрасывается
    r.forced_supply = 20;
    r.run(sec(30));
    assert(r.sys.fans().supply_level() == 3);  // взведено до ручного сброса
    r.sys.reset_alarms(r.now, 0);
    assert(!r.sys.alarms().on(AL_OVERHEAT) && !r.sys.alarms().on(AL_HEAT_UNCONTROLLED));
    r.override_supply = false;
    r.run(sec(60));
    assert(r.sys.fans().state() == FanState::RUNNING && r.sys.fans().supply_level() == 2);
    std::printf("3. перегрев: продувка до остывания, охлаждение, ручной сброс — OK\n");
  }

  // 3b. Пробит SSR на малом расходе (до 45 °C не доходит): обнаружение по «тепло без команды»
  {
    Rig r;
    r.plant.max_rise = 33;  // 1,5 кВт на ~135 м³/ч
    r.run(sec(10 * 60));
    assert(std::fabs(r.in.t_supply - 18.0f) < 0.5f);
    r.plant.ssr_welded = true;
    r.run(sec(6 * 60));
    assert(r.sys.alarms().on(AL_HEAT_UNCONTROLLED) && !r.sys.alarms().on(AL_OVERHEAT));
    assert(r.sys.fans().supply_level() == 3 && r.sys.status().heater_pct == 0);
    r.in.mode = 0;  // даже «Стоп» не останавливает охлаждение
    r.run(sec(5 * 60));
    assert(r.sys.fans().supply_level() == 3);
    std::printf("3b. пробит SSR: авария, вентиляторы на максимум и не останавливаются — OK\n");
  }

  // 4. Обрыв датчика притока: стоп, сам восстанавливается
  {
    Rig r;
    r.run(sec(5 * 60));
    r.override_supply = true;
    r.forced_supply = NAN;
    r.run(TICK);
    assert(r.sys.status().heater_pct == 0 && r.sys.alarms().on(AL_SENSOR_SUPPLY));
    r.run(sec(200));
    assert(r.stopped());
    r.override_supply = false;
    r.run(sec(60));
    assert(r.sys.fans().state() == FanState::RUNNING && r.sys.status().ok);
    std::printf("4. обрыв датчика: стоп и автоматический повторный пуск — OK\n");
  }

  // 5. Нет потока (реле давления не замкнулось): авария через 30 с, ручной сброс
  {
    Rig r;
    r.auto_flow = false;
    r.in.flow_ok = false;
    r.run(sec(80));
    assert(r.sys.alarms().on(AL_NO_FLOW) && r.max_heat == 0);
    r.in.flow_ok = true;
    r.run(sec(200));
    assert(r.stopped() && r.sys.alarms().on(AL_NO_FLOW));
    std::printf("5. нет потока: авария, нагрев не включался — OK\n");
  }

  // 6. Пожар: всё сразу выключено, заслонка закрыта, без продувки; сброс после снятия сигнала
  {
    Rig r;
    r.run(sec(5 * 60));
    r.in.fire_ok = false;
    r.run(TICK);
    const VentStatus &o = r.sys.status();
    assert(r.salda.supply_mask() == 0 && r.salda.exhaust_mask() == 0 && !r.salda.k[6].v);
    assert(o.heater_pct == 0 && !o.preheater && !r.salda.ssr_preheater.v);
    r.sys.reset_alarms(r.now, 0);
    r.run(sec(30));
    assert(r.sys.alarms().on(AL_FIRE) && r.stopped());
    r.in.fire_ok = true;
    r.run(sec(30));
    assert(r.stopped());
    r.sys.reset_alarms(r.now, 0);
    r.run(sec(60));
    assert(r.sys.fans().state() == FanState::RUNNING);
    std::printf("6. пожар: мгновенный стоп, заслонка закрыта — OK\n");
  }
  // 6b. Пожар с продувкой (параметр)
  {
    SaldaOptions o;
    o.sys.fire_purge = true;
    Rig r(o);
    r.run(sec(5 * 60));
    r.in.fire_ok = false;
    r.run(TICK);
    assert(r.sys.fans().state() == FanState::PURGE && r.sys.status().heater_pct == 0);
  }

  // 7. Обмерзание: преднагрев → снижение притока → только вытяжка → 30 мин → полный стоп
  {
    Rig r;
    r.in.t_exhaust = -2;
    r.run(sec(60));
    assert(r.sys.status().preheater);
    r.run(sec(10 * 60));
    assert(r.sys.fans().supply_level() == 1 && r.sys.fans().exhaust_level() == 2);
    assert(r.sys.alarms().on(AL_FROST_REDUCED));
    r.run(sec(10 * 60));  // следующий шаг: вытяжка усилена
    assert(r.sys.fans().supply_level() == 1 && r.sys.fans().exhaust_level() == 3);
    r.run(sec(13 * 60));  // последний шаг + продувка 120 с перед остановом притока
    assert(r.sys.fans().state() == FanState::EXHAUST_ONLY);
    assert(r.sys.fans().supply_level() == 0 && r.sys.fans().exhaust_level() > 0);
    assert(r.sys.alarms().on(AL_FROST_EXHAUST_ONLY));
    r.run(sec(31 * 60));
    assert(r.sys.alarms().on(AL_FROST) && r.stopped());
    std::printf("7. обмерзание: приток ниже, вытяжка выше, только вытяжка, стоп через 30 мин — OK\n");
  }
  // 7b. Оттайка помогла: приток возвращается
  {
    Rig r;
    r.in.t_exhaust = -2;
    r.run(sec(34 * 60));
    assert(r.sys.fans().state() == FanState::EXHAUST_ONLY);
    r.in.t_exhaust = 8;
    r.run(sec(11 * 60));
    assert(r.sys.fans().supply_level() == 1 && r.sys.fans().exhaust_level() == 3);
    assert(!r.sys.alarms().on(AL_FROST));
    r.run(sec(10 * 60));
    assert(r.sys.fans().supply_level() == 1 && r.sys.fans().exhaust_level() == 2);
    r.run(sec(10 * 60));
    assert(r.sys.fans().supply_level() == 2 && r.sys.frost_level() == 0);
    std::printf("7b. оттайка: приток восстановлен по ступеням — OK\n");
  }
  // 7c. Вытяжка без притока запрещена: сразу полный стоп
  {
    SaldaOptions o;
    o.recup.forbid_imbalance = true;
    Rig r(o);
    r.in.t_exhaust = -2;
    r.run(sec(11 * 60));  // приток и вытяжка снижены вместе — баланс сохранён
    assert(r.sys.fans().supply_level() == 1 && r.sys.fans().exhaust_level() == 1);
    r.run(sec(11 * 60));
    assert(r.sys.alarms().on(AL_FROST));
    assert(!r.sys.alarms().on(AL_FROST_EXHAUST_ONLY));
    r.run(sec(200));
    assert(r.stopped());
    std::printf("7c. запрет вытяжки без притока: полный стоп — OK\n");
  }

  // 8. Внешний стоп: с продувкой, «норма» сохраняется, сам возобновляется
  {
    Rig r;
    r.run(sec(5 * 60));
    r.in.ext_stop_ok = false;
    r.run(TICK);
    assert(r.sys.fans().state() == FanState::PURGE && r.sys.status().ok);
    r.run(sec(200));
    assert(r.stopped());
    r.in.ext_stop_ok = true;
    r.run(sec(60));
    assert(r.sys.fans().state() == FanState::RUNNING);
    std::printf("8. внешний стоп — OK\n");
  }

  // 9. Перезагрузка с невзведённой аварией: остаёмся в стопе до ручного сброса
  {
    Rig r;
    r.salda.th100.v = false;
    r.run(sec(20));
    r.salda.th100.v = true;
    r.run(sec(5));
    uint32_t words[JOURNAL_WORDS];
    r.sys.alarms().export_journal(words);
    const uint32_t latched = r.sys.alarms().latched_mask();

    Rig r2;  // «после перезагрузки»
    r2.sys.alarms().import(words, latched);
    r2.run(sec(120));
    assert(r2.stopped() && r2.sys.alarms().on(AL_THERMOSTAT));
    r2.sys.reset_alarms(r2.now, 0);
    r2.run(sec(60));
    assert(r2.sys.fans().state() == FanState::RUNNING);
    assert(r2.sys.alarms().journal_count() >= 3);
    char buf[160];
    for (int i = r2.sys.alarms().journal_count() - 1; i >= 0; i--) {
      format_journal_entry(r2.sys.alarms().journal(i), buf, sizeof(buf));
      std::printf("   журнал: %s\n", buf);
    }
    std::printf("9. авария сохраняется после перезагрузки — OK\n");
  }

  // 10. Предупреждение о фильтре не останавливает установку
  {
    Rig r;
    r.in.filter_supply_ok = false;
    r.run(sec(5 * 60));
    assert(r.sys.fans().state() == FanState::RUNNING && r.sys.status().ok);
    char buf[160];
    r.sys.status_text(buf, sizeof(buf));
    std::printf("10. фильтр: «%s» — OK\n", buf);
  }

  // 11. Пропала связь с PCF8574: нагрев сразу выкл, стоп, после восстановления — пуск
  {
    Rig r;
    r.run(sec(5 * 60));
    r.in.io_ok = false;
    r.run(TICK);
    assert(r.sys.status().heater_pct == 0 && !r.sys.status().ok);
    r.run(sec(200));
    assert(r.stopped());
    r.in.io_ok = true;
    r.run(sec(60));
    assert(r.sys.fans().state() == FanState::RUNNING);
    std::printf("11. нет связи с PCF8574 — OK\n");
  }

  std::printf("OK\n");
  return 0;
}
