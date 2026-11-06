// Хост-тест последовательности пуска/останова (vent/sequencer.h) с драйверами
// AC-мотора с отводами (vent/fan_ac_taps.h) и заслонки с пружинным возвратом.
// Сборка: g++ -std=c++17 -I../firmware/include -I. test_vent_fans.cpp -o t && ./t
#include <cassert>
#include <cstdio>
#include <memory>

#include "salda_profile.h"

using namespace vent;

static const uint32_t TICK = 100;

struct Sim {
  fake::Out k[10];
  std::unique_ptr<AcTapsFan> sup, exh;
  std::unique_ptr<SpringDamper> damper;
  fake::NullSink sink;
  FanSequencer c;
  SequencerConfig cfg;
  uint32_t now = 0;
  uint8_t taps;

  // has_supply/has_exhaust — тип установки; taps — число ступеней моторов.
  Sim(bool has_damper = true, bool has_supply = true, bool has_exhaust = true, uint8_t n = 3) : taps(n) {
    DigitalOut *s[5], *e[5];
    for (int i = 0; i < n; i++) {
      s[i] = &k[i];
      e[i] = &k[5 + i];
    }
    if (has_supply) sup.reset(new AcTapsFan(s, n, 1500));
    if (has_exhaust) exh.reset(new AcTapsFan(e, n, 1500));
    if (has_damper) damper.reset(new SpringDamper(&k[9], 30000));
    c.configure(cfg, sup.get(), exh.get(), damper.get());
  }

  int bits(int from) const {
    int b = 0;
    for (int i = 0; i < taps; i++) b += k[from + i].v;
    return b;
  }
  bool damper_cmd() const { return k[9].v; }

  // Крутит время, проверяя инварианты на каждом шаге.
  void run(uint32_t ms, FanRequest req) {
    for (uint32_t t = 0; t < ms; t += TICK) {
      now += TICK;
      if (damper) damper->update(now, sink);
      c.update(now, req);
      assert(bits(0) <= 1);  // одна ступень на мотор
      assert(bits(5) <= 1);
      if (exh)  // приток никогда не работает без вытяжки
        assert(!(c.supply_level() > 0 && c.exhaust_level() == 0));
      if (c.heat_permitted(now))  // нагрев только при работающем притоке
        assert(c.supply_level() > 0 && c.state() == FanState::RUNNING);
    }
  }
  template <typename F> uint32_t until(F cond, FanRequest req, uint32_t limit = 600000) {
    uint32_t start = now;
    while (!cond()) {
      run(TICK, req);
      assert(now - start < limit);
    }
    return now - start;
  }
};

int main() {
  const SequencerConfig cfg;  // значения по умолчанию = параметры прошивки
  const uint32_t damper_ms = 30000, pause_ms = 1500;
  const FanRequest STOP{};
  const FanRequest M2 = speeds_from_mode(2, 0, 3);

  // --- Политика режима ---
  assert(speeds_from_mode(1, -1, 3).exhaust == 1);
  assert(speeds_from_mode(3, +1, 3).exhaust == 3);
  assert(speeds_from_mode(2, +1, 3).exhaust == 3);
  assert(speeds_from_mode(0, +1, 3).exhaust == 0);
  assert(speeds_from_mode(5, 0, 5).supply == 5);

  // --- Пуск с заслонкой ---
  {
    Sim s;
    s.run(1000, STOP);
    assert(s.c.state() == FanState::STOPPED && !s.damper_cmd());
    s.run(TICK, M2);
    assert(s.c.state() == FanState::DAMPER_OPENING && s.damper_cmd());
    assert(s.c.exhaust_level() == 0);
    uint32_t t_exh = s.until([&] { return s.c.exhaust_level() > 0; }, M2);
    assert(t_exh >= damper_ms && t_exh <= damper_ms + 500);
    uint32_t t_sup = s.until([&] { return s.c.supply_level() > 0; }, M2);
    assert(t_sup >= cfg.exhaust_lead_ms && t_sup <= cfg.exhaust_lead_ms + 500);
    assert(!s.c.heat_permitted(s.now));
    uint32_t t_heat = s.until([&] { return s.c.heat_permitted(s.now); }, M2);
    assert(t_heat >= cfg.heat_delay_ms - TICK && t_heat <= cfg.heat_delay_ms + 500);
    std::printf("пуск: вытяжка через %u мс, приток +%u мс, нагрев +%u мс\n", t_exh, t_sup, t_heat);

    // --- Смена ступени 2 → 3: пауза, нагрев запрещён на время паузы ---
    const FanRequest M3 = speeds_from_mode(3, 0, 3);
    s.run(TICK, M3);
    assert(s.c.supply_level() == 0 && !s.c.heat_permitted(s.now));
    uint32_t t_sw = s.until([&] { return s.c.supply_level() == 3; }, M3);
    assert(t_sw >= pause_ms - 2 * TICK);
    uint32_t t_heat2 = s.until([&] { return s.c.heat_permitted(s.now); }, M3);
    assert(t_heat2 >= cfg.heat_delay_ms - TICK);
    std::printf("смена ступени: пауза %u мс, нагрев снова через %u мс\n", t_sw, t_heat2);

    // --- Останов: продувка → стоп притока → стоп вытяжки → выбег → заслонка ---
    s.run(TICK, STOP);
    assert(s.c.state() == FanState::PURGE && !s.c.heat_permitted(s.now));
    assert(s.c.supply_level() == 3);
    uint32_t t_purge = s.until([&] { return s.c.supply_level() == 0; }, STOP);
    assert(t_purge >= cfg.purge_ms - TICK && t_purge <= cfg.purge_ms + 500);
    assert(s.c.exhaust_level() > 0 && s.damper_cmd());
    uint32_t t_exh_stop = s.until([&] { return s.c.exhaust_level() == 0; }, STOP);
    assert(t_exh_stop >= cfg.exhaust_lead_ms - TICK);
    assert(s.damper_cmd());
    uint32_t t_damper = s.until([&] { return !s.damper_cmd(); }, STOP);
    assert(t_damper >= cfg.rundown_ms - TICK);
    assert(s.c.state() == FanState::STOPPED);
    std::printf("останов: продувка %u мс, вытяжка +%u мс, заслонка +%u мс\n", t_purge, t_exh_stop, t_damper);
  }

  // --- Повторный пуск во время продувки: обратно в РАБОТУ ---
  {
    Sim s;
    s.until([&] { return s.c.heat_permitted(s.now); }, M2);
    s.run(10000, STOP);
    assert(s.c.state() == FanState::PURGE);
    s.run(TICK, M2);
    assert(s.c.state() == FanState::RUNNING);
    assert(s.c.heat_permitted(s.now));  // приток не останавливался
  }

  // --- Без заслонки: вытяжка сразу ---
  {
    Sim s(false);
    uint32_t t = s.until([&] { return s.c.exhaust_level() > 0; }, M2);
    assert(t <= 500);
  }

  // --- Аварийный стоп: всё сразу, без продувки ---
  {
    Sim s;
    s.until([&] { return s.c.heat_permitted(s.now); }, M2);
    FanRequest em = M2;
    em.emergency_stop = true;
    s.run(TICK, em);
    assert(s.c.supply_level() == 0 && s.c.exhaust_level() == 0 && !s.c.heat_permitted(s.now));
    s.run(cfg.rundown_ms + 1000, em);
    assert(s.c.state() == FanState::STOPPED && !s.damper_cmd());
    s.run(5000, em);
    assert(s.c.state() == FanState::STOPPED);
  }

  // --- Смещение вытяжки −1 на ступени 1: вытяжка не останавливается ---
  {
    Sim s;
    FanRequest r = speeds_from_mode(1, -1, 3);
    s.until([&] { return s.c.state() == FanState::RUNNING && s.c.supply_level() == 1; }, r);
    assert(s.c.exhaust_level() == 1);
  }

  // --- Только вытяжка (оттайка) и возврат притока ---
  {
    Sim s;
    s.until([&] { return s.c.heat_permitted(s.now); }, M2);
    FanRequest ex = M2;
    ex.supply = 0;
    s.run(TICK, ex);
    assert(s.c.state() == FanState::PURGE && !s.c.heat_permitted(s.now));
    s.run(cfg.purge_ms + 1000, ex);
    assert(s.c.state() == FanState::EXHAUST_ONLY);
    assert(s.c.supply_level() == 0 && s.c.exhaust_level() == 2 && s.damper_cmd());
    s.run(TICK, M2);
    assert(s.c.state() == FanState::RUNNING);
    uint32_t t_heat = s.until([&] { return s.c.heat_permitted(s.now); }, M2);
    assert(t_heat >= cfg.heat_delay_ms - TICK);
  }

  // --- Пожар: немедленный стоп и закрытие заслонки без выбега ---
  {
    Sim s;
    s.until([&] { return s.c.state() == FanState::RUNNING; }, M2);
    FanRequest fire;
    fire.emergency_stop = true;
    fire.close_damper_now = true;
    s.run(TICK, fire);
    assert(s.c.supply_level() == 0 && s.c.exhaust_level() == 0 && !s.damper_cmd());
  }

  // --- Удержание продувки: пока приток горячий, приток не останавливается ---
  {
    Sim s;
    s.until([&] { return s.c.heat_permitted(s.now); }, M2);
    FanRequest hot;
    hot.hold_purge = true;
    s.run(cfg.purge_ms * 2, hot);
    assert(s.c.state() == FanState::PURGE && s.c.supply_level() > 0);
    s.run(TICK, STOP);
    assert(s.c.supply_level() == 0);
  }

  // --- Приточная установка (без вытяжки) ---
  {
    Sim s(true, true, false);
    uint32_t t = s.until([&] { return s.c.supply_level() > 0; }, M2);
    assert(t >= damper_ms && t <= damper_ms + 500);  // сразу после заслонки, без задержки вытяжки
    s.until([&] { return s.c.heat_permitted(s.now); }, M2);
    s.run(TICK, STOP);
    assert(s.c.state() == FanState::PURGE);
    s.until([&] { return s.c.state() == FanState::STOPPED; }, STOP);
    assert(!s.damper_cmd());
    std::printf("приточная: пуск после заслонки, продувка и стоп — OK\n");
  }

  // --- Вытяжная установка (без притока): нагрева нет, без продувки ---
  {
    Sim s(true, false, true);
    FanRequest r = speeds_from_mode(2, 0, 3);
    s.until([&] { return s.c.state() == FanState::RUNNING; }, r);
    assert(s.c.exhaust_level() == 2 && !s.c.heat_permitted(s.now));
    s.run(TICK, STOP);
    s.until([&] { return s.c.state() == FanState::STOPPED; }, STOP, 30000 + 15000 + 5000);
    std::printf("вытяжная: пуск и стоп без продувки — OK\n");
  }

  // --- AC-мотор на 5 ступеней: переходы только через «всё выключено» ---
  {
    Sim s(false, true, true, 5);
    FanRequest r5 = speeds_from_mode(5, 0, 5);
    s.until([&] { return s.c.supply_level() == 5; }, r5);
    FanRequest r1 = speeds_from_mode(1, 0, 5);
    s.until([&] { return s.c.supply_level() == 1; }, r1);
    std::printf("AC 5 ступеней — OK\n");
  }

  std::printf("OK\n");
  return 0;
}
