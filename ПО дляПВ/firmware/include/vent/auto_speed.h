#pragma once
// Авторежим: ступень вентиляции по CO2 (и подъём по влажности).
//
//  CO2 ≤ co2_level2            → ступень 1
//  co2_level2 < CO2 ≤ level3   → ступень 2
//  CO2 > co2_level3            → ступень 3
// Вниз — с гистерезисом co2_hyst. Влажность выше rh_boost_above → не ниже rh_boost_level
// (снятие ниже rh_boost_above − rh_boost_hyst).
// Чтобы ступень не «прыгала»: вверх — если новая ступень держится up_delay_ms,
// вниз — down_delay_ms. Первое показание после пуска применяется сразу.
// Нет данных CO2 → резервная ступень fallback_level (система выдаёт предупреждение).

#include <cmath>
#include <cstdint>

namespace vent {

struct AutoSpeedConfig {
  float co2_level2{800.0f};   // ppm
  float co2_level3{1100.0f};  // ppm
  float co2_hyst{50.0f};
  bool rh_enabled{true};
  float rh_boost_above{70.0f};  // %
  float rh_boost_hyst{5.0f};
  uint8_t rh_boost_level{3};
  uint32_t up_delay_ms{60000};
  uint32_t down_delay_ms{600000};
  uint8_t fallback_level{2};
};

class AutoSpeed {
 public:
  void configure(const AutoSpeedConfig &cfg) { cfg_ = cfg; }

  // Сброс при выходе из авторежима: следующее включение начнёт с текущих показаний.
  void reset() {
    init_ = false;
    missing_ = false;
  }

  uint8_t update(uint32_t now, float co2, float rh, uint8_t max_level) {
    const uint8_t top = max_level < 3 ? max_level : 3;
    if (std::isnan(co2)) {
      missing_ = true;
      init_ = false;
      level_ = clamp_(cfg_.fallback_level, top);
      return level_;
    }
    missing_ = false;

    // ступень по CO2: вверх по порогам, вниз — с гистерезисом от текущей
    const uint8_t up = 1 + (co2 > cfg_.co2_level2) + (co2 > cfg_.co2_level3);
    const uint8_t down = 1 + (co2 > cfg_.co2_level2 - cfg_.co2_hyst) + (co2 > cfg_.co2_level3 - cfg_.co2_hyst);
    uint8_t want = init_ ? co2_level_ : up;
    if (up > want) want = up;
    else if (down < want) want = down;
    co2_level_ = want;

    // влажность
    if (cfg_.rh_enabled && !std::isnan(rh)) {
      if (rh > cfg_.rh_boost_above) rh_high_ = true;
      else if (rh < cfg_.rh_boost_above - cfg_.rh_boost_hyst) rh_high_ = false;
    } else {
      rh_high_ = false;
    }
    if (rh_high_ && cfg_.rh_boost_level > want)
      want = cfg_.rh_boost_level;
    want = clamp_(want, top);

    if (!init_) {
      init_ = true;
      level_ = want;
      pending_ = want;
      return level_;
    }
    if (want == level_) {
      pending_ = want;
      return level_;
    }
    if (want != pending_) {  // новое направление — отсчёт заново
      pending_ = want;
      pending_since_ = now;
    }
    const uint32_t delay = want > level_ ? cfg_.up_delay_ms : cfg_.down_delay_ms;
    if (now - pending_since_ >= delay)
      level_ = want;
    return level_;
  }

  uint8_t level() const { return level_; }
  bool sensor_missing() const { return missing_; }
  bool humidity_boost() const { return rh_high_; }

 private:
  static uint8_t clamp_(uint8_t l, uint8_t top) { return l < 1 ? 1 : (l > top ? top : l); }

  AutoSpeedConfig cfg_{};
  bool init_{false};
  bool missing_{false};
  bool rh_high_{false};
  uint8_t co2_level_{1};
  uint8_t level_{1};
  uint8_t pending_{1};
  uint32_t pending_since_{0};
};

}  // namespace vent
