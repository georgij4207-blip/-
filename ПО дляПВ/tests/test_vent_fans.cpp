// Хост-тест firmware/include/vent_fans.h: последовательности пуска/останова.
// Сборка: g++ -std=c++17 -I../firmware/include test_vent_fans.cpp -o t && ./t
#include <cassert>
#include <cstdio>
#include "vent_fans.h"

using namespace vent;

static const uint32_t TICK = 100;

struct Sim {
  FanController c;
  uint32_t now = 0;
  int max_supply_bits = 0, max_exhaust_bits = 0;

  explicit Sim(FanConfig cfg) { c.configure(cfg); }

  static int bits(uint8_t m) { return __builtin_popcount(m); }

  // Крутит время, проверяя инварианты на каждом шаге.
  void run(uint32_t ms, FanRequest req) {
    for (uint32_t t = 0; t < ms; t += TICK) {
      now += TICK;
      c.update(now, req);
      assert(bits(c.supply_relay_mask()) <= 1);
      assert(bits(c.exhaust_relay_mask()) <= 1);
      // приток никогда не работает без вытяжки
      assert(!(c.supply_speed() > 0 && c.exhaust_speed() == 0));
      // нагрев только при работающем притоке
      if (c.heat_permitted(now))
        assert(c.supply_speed() > 0 && c.state() == FanState::RUNNING);
    }
  }
  // Время до выполнения условия.
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
  FanConfig cfg;  // значения по умолчанию = параметры прошивки
  const FanRequest STOP{};
  const FanRequest M2 = speeds_from_mode(2, 0);

  // --- Политика режима ---
  assert(speeds_from_mode(1, -1).exhaust == 1);
  assert(speeds_from_mode(3, +1).exhaust == 3);
  assert(speeds_from_mode(2, +1).exhaust == 3);
  assert(speeds_from_mode(0, +1).exhaust == 0);

  // --- Пуск с заслонкой ---
  {
    Sim s(cfg);
    s.run(1000, STOP);
    assert(s.c.state() == FanState::STOPPED && !s.c.damper_open_cmd());
    s.run(TICK, M2);
    assert(s.c.state() == FanState::DAMPER_OPENING && s.c.damper_open_cmd());
    assert(s.c.exhaust_speed() == 0);
    uint32_t t_exh = s.until([&] { return s.c.exhaust_speed() > 0; }, M2);
    assert(t_exh >= cfg.damper_open_ms && t_exh <= cfg.damper_open_ms + 500);
    uint32_t t_sup = s.until([&] { return s.c.supply_speed() > 0; }, M2);
    assert(t_sup >= cfg.exhaust_lead_ms && t_sup <= cfg.exhaust_lead_ms + 500);
    assert(!s.c.heat_permitted(s.now));
    uint32_t t_heat = s.until([&] { return s.c.heat_permitted(s.now); }, M2);
    assert(t_heat >= cfg.heat_delay_ms - TICK && t_heat <= cfg.heat_delay_ms + 500);
    std::printf("пуск: вытяжка через %u мс, приток +%u мс, нагрев +%u мс\n", t_exh, t_sup, t_heat);

    // --- Смена ступени 2 → 3: пауза, нагрев запрещён на время паузы ---
    const FanRequest M3 = speeds_from_mode(3, 0);
    s.run(TICK, M3);
    assert(s.c.supply_speed() == 0 && !s.c.heat_permitted(s.now));
    uint32_t t_sw = s.until([&] { return s.c.supply_speed() == 3; }, M3);
    assert(t_sw >= cfg.switch_pause_ms - 2 * TICK);
    uint32_t t_heat2 = s.until([&] { return s.c.heat_permitted(s.now); }, M3);
    assert(t_heat2 >= cfg.heat_delay_ms - TICK);
    std::printf("смена ступени: пауза %u мс, нагрев снова через %u мс\n", t_sw, t_heat2);

    // --- Останов: продувка → стоп притока → стоп вытяжки → выбег → заслонка ---
    s.run(TICK, STOP);
    assert(s.c.state() == FanState::PURGE && !s.c.heat_permitted(s.now));
    assert(s.c.supply_speed() == 3);
    uint32_t t_purge = s.until([&] { return s.c.supply_speed() == 0; }, STOP);
    assert(t_purge >= cfg.purge_ms - TICK && t_purge <= cfg.purge_ms + 500);
    assert(s.c.exhaust_speed() > 0 && s.c.damper_open_cmd());
    uint32_t t_exh_stop = s.until([&] { return s.c.exhaust_speed() == 0; }, STOP);
    assert(t_exh_stop >= cfg.exhaust_lead_ms - TICK);
    assert(s.c.damper_open_cmd());
    uint32_t t_damper = s.until([&] { return !s.c.damper_open_cmd(); }, STOP);
    assert(t_damper >= cfg.rundown_ms - TICK);
    assert(s.c.state() == FanState::STOPPED);
    std::printf("останов: продувка %u мс, вытяжка +%u мс, заслонка +%u мс\n", t_purge, t_exh_stop, t_damper);
  }

  // --- Повторный пуск во время продувки: обратно в РАБОТУ ---
  {
    Sim s(cfg);
    s.until([&] { return s.c.heat_permitted(s.now); }, M2);
    s.run(10000, STOP);
    assert(s.c.state() == FanState::PURGE);
    s.run(TICK, M2);
    assert(s.c.state() == FanState::RUNNING);
    // приток не останавливался, поток был — нагрев разрешён без новой задержки
    assert(s.c.heat_permitted(s.now));
  }

  // --- Без заслонки: K7 не трогаем, вытяжка сразу ---
  {
    FanConfig nd = cfg;
    nd.damper_enabled = false;
    Sim s(nd);
    s.run(TICK, M2);
    assert(!s.c.damper_open_cmd());
    uint32_t t = s.until([&] { return s.c.exhaust_speed() > 0; }, M2);
    assert(t <= 500);
  }

  // --- Аварийный стоп: всё сразу, без продувки ---
  {
    Sim s(cfg);
    s.until([&] { return s.c.heat_permitted(s.now); }, M2);
    FanRequest em = M2;
    em.emergency_stop = true;
    s.run(TICK, em);
    assert(s.c.supply_speed() == 0 && s.c.exhaust_speed() == 0 && !s.c.heat_permitted(s.now));
    s.run(cfg.rundown_ms + 1000, em);
    assert(s.c.state() == FanState::STOPPED && !s.c.damper_open_cmd());
    s.run(5000, em);  // пока авария активна — не стартует
    assert(s.c.state() == FanState::STOPPED);
  }

  // --- Смещение вытяжки −1 на ступени 1: вытяжка не останавливается ---
  {
    Sim s(cfg);
    FanRequest r = speeds_from_mode(1, -1);
    s.until([&] { return s.c.state() == FanState::RUNNING && s.c.supply_speed() == 1; }, r);
    assert(s.c.exhaust_speed() == 1);
  }

  std::printf("OK\n");
  return 0;
}
