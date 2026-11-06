// Хост-тест авторежима по CO2/влажности (vent/auto_speed.h) и его работы в системе.
// Сборка: g++ -std=c++17 -I../firmware/include -I. test_auto_speed.cpp -o t && ./t
#include <cassert>
#include <cstdio>

#include "salda_profile.h"

using namespace vent;

static uint32_t sec(uint32_t s) { return s * 1000; }

int main() {
  AutoSpeedConfig cfg;  // 800/1100 ppm, гистерезис 50, вверх 60 с, вниз 600 с
  // --- 1. Первое показание — сразу ---
  {
    AutoSpeed a;
    a.configure(cfg);
    assert(a.update(0, 600, NAN, 3) == 1);
    a.reset();
    assert(a.update(0, 900, NAN, 3) == 2);
    a.reset();
    assert(a.update(0, 1500, NAN, 3) == 3);
  }
  // --- 2. Вверх через 60 с, короткий всплеск не поднимает ---
  {
    AutoSpeed a;
    a.configure(cfg);
    uint32_t t = 0;
    a.update(t, 600, NAN, 3);
    for (t = 1000; t < sec(30); t += 1000) assert(a.update(t, 1200, NAN, 3) == 1);
    assert(a.update(t, 600, NAN, 3) == 1);  // всплеск кончился
    uint32_t start = t + 1000;
    for (t = start; t < start + sec(59); t += 1000) assert(a.update(t, 950, NAN, 3) == 1);
    assert(a.update(start + sec(61), 950, NAN, 3) == 2);
    std::printf("1–2. пороги и задержка подъёма — OK\n");

    // --- 3. Вниз: гистерезис 50 ppm и 10 мин ---
    uint32_t t2 = start + sec(62);
    assert(a.update(t2, 770, NAN, 3) == 2);  // 770 > 800 − 50: остаёмся на 2
    for (uint32_t k = 0; k < 700; k++) assert(a.update(t2 + sec(k), 770, NAN, 3) == 2);
    uint32_t t3 = t2 + sec(701);
    a.update(t3, 700, NAN, 3);
    assert(a.update(t3 + sec(599), 700, NAN, 3) == 2);
    assert(a.update(t3 + sec(601), 700, NAN, 3) == 1);
    std::printf("3. гистерезис и задержка спуска — OK\n");
  }
  // --- 4. Влажность: подъём до 3, снятие ниже 65 % ---
  {
    AutoSpeed a;
    a.configure(cfg);
    a.update(0, 600, 50, 3);
    a.update(sec(1), 600, 75, 3);
    assert(a.update(sec(62), 600, 75, 3) == 3 && a.humidity_boost());
    a.update(sec(63), 600, 68, 3);  // 68 > 65: ещё держим
    assert(a.humidity_boost());
    a.update(sec(64), 600, 60, 3);
    assert(!a.humidity_boost());
    assert(a.update(sec(64 + 601), 600, 60, 3) == 1);
    std::printf("4. подъём по влажности — OK\n");
  }
  // --- 5. Нет данных CO2: резервная ступень ---
  {
    AutoSpeed a;
    a.configure(cfg);
    assert(a.update(0, NAN, NAN, 3) == 2 && a.sensor_missing());
    assert(a.update(sec(1), 1500, NAN, 3) == 3 && !a.sensor_missing());  // вернулся — сразу
    std::printf("5. резервная ступень без датчика — OK\n");
  }
  // --- 6. В системе: режим «Авто» ведёт вентиляторы, без датчика — предупреждение ---
  {
    SaldaRig r;
    VentInputs in;
    in.mode = MODE_AUTO;
    in.t_outdoor = 0;
    in.t_supply = 18;
    in.t_extract = 22;
    in.t_exhaust = 8;
    in.flow_ok = true;
    in.co2_ppm = 1300;
    uint32_t now = 0;
    for (; now < sec(120); now += 200) r.sys.update(now, in);
    assert(r.sys.status().auto_mode && r.sys.status().auto_level == 3);
    assert(r.sys.fans().supply_level() == 3);
    in.co2_ppm = NAN;
    for (uint32_t end = now + sec(10); now < end; now += 200) r.sys.update(now, in);
    assert(r.sys.alarms().on(AL_CO2_SENSOR) && r.sys.status().ok);  // предупреждение, не авария
    assert(r.sys.status().auto_level == 2);
    in.mode = 1;  // ручной режим — предупреждение снимается
    for (uint32_t end = now + sec(5); now < end; now += 200) r.sys.update(now, in);
    assert(!r.sys.alarms().on(AL_CO2_SENSOR) && !r.sys.status().auto_mode);
    std::printf("6. авторежим в системе — OK\n");
  }
  std::printf("OK\n");
  return 0;
}
