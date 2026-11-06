#pragma once
// AC-мотор с отводами обмотки: одна ступень = одно реле (2–5 ступеней).
// Смена ступени всегда через «всё выключено» и паузу; одновременно включено не больше
// одного реле. Аппаратная цепочка NC-контактов (docs/fan_wiring.md) страхует прошивку.

#include "fan.h"

namespace vent {

class AcTapsFan : public FanDriver {
 public:
  static const uint8_t MAX_TAPS = 5;

  // taps[0] — реле ступени 1 … taps[n-1] — ступени n.
  AcTapsFan(DigitalOut *const *taps, uint8_t n, uint32_t pause_ms) : pause_ms_(pause_ms) {
    n_ = n > MAX_TAPS ? MAX_TAPS : n;
    for (uint8_t i = 0; i < n_; i++)
      taps_[i] = taps[i];
  }

  const char *type_name() const override { return "AC, отводы обмотки"; }
  uint8_t levels() const override { return n_; }
  void set_target(uint8_t level) override { target_ = level > n_ ? n_ : level; }

  void update(uint32_t now) override {
    if (active_ != target_) {
      if (active_ != 0) {
        active_ = 0;
        off_since_ = now;
        pause_pending_ = true;
      } else if (!pause_pending_ || now - off_since_ >= pause_ms_) {
        pause_pending_ = false;
        active_ = target_;
      }
    }
    // Сначала выключения, потом включение — на выходах не бывает двух ступеней.
    for (uint8_t i = 0; i < n_; i++)
      if (active_ != i + 1)
        taps_[i]->set(false);
    if (active_ > 0)
      taps_[active_ - 1]->set(true);
  }

  uint8_t active_level() const override { return active_; }
  bool settled() const override { return active_ == target_; }

 private:
  DigitalOut *taps_[MAX_TAPS]{};
  uint8_t n_{0};
  uint8_t target_{0};
  uint8_t active_{0};
  uint32_t pause_ms_;
  uint32_t off_since_{0};
  bool pause_pending_{false};
};

}  // namespace vent
