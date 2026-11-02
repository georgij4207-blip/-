#pragma once
// Только для прошивки: обёртки сущностей ESPHome в каналы hal.h.
// Драйверы создаются в packages/drivers/*.yaml так:
//   new vent::AcTapsFan(..., vent::esph::out(id(relay_k1)), ...)
// Объекты живут всё время работы (не удаляются).

#ifdef USE_ESP32
#include "esphome/core/defines.h"

#include <cmath>

#include "esphome/core/hal.h"
#include "hal.h"

namespace vent {
namespace esph {

#ifdef USE_SWITCH
class SwitchOut : public DigitalOut {
 public:
  explicit SwitchOut(esphome::switch_::Switch *s) : s_(s) {}
  void set(bool on) override {
    if (s_->state != on) {
      if (on) s_->turn_on(); else s_->turn_off();
    }
  }
  bool get() const override { return s_->state; }

 private:
  esphome::switch_::Switch *s_;
};
inline DigitalOut *out(esphome::switch_::Switch *s) { return new SwitchOut(s); }
#endif

#ifdef USE_OUTPUT
class BinaryOut : public DigitalOut {
 public:
  explicit BinaryOut(esphome::output::BinaryOutput *o) : o_(o) {}
  void set(bool on) override {
    if (!init_ || on != state_) {
      if (on) o_->turn_on(); else o_->turn_off();
      state_ = on;
      init_ = true;
    }
  }
  bool get() const override { return state_; }

 private:
  esphome::output::BinaryOutput *o_;
  bool state_{false};
  bool init_{false};
};
inline DigitalOut *bout(esphome::output::BinaryOutput *o) { return new BinaryOut(o); }

class FloatOut : public AnalogOut {
 public:
  explicit FloatOut(esphome::output::FloatOutput *o) : o_(o) {}
  void set(float f) override {
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    if (f != value_) {
      o_->set_level(f);
      value_ = f;
    }
  }
  float get() const override { return value_ < 0.0f ? 0.0f : value_; }

 private:
  esphome::output::FloatOutput *o_;
  float value_{-1.0f};
};
inline AnalogOut *aout(esphome::output::FloatOutput *o) { return new FloatOut(o); }
#endif

#ifdef USE_BINARY_SENSOR
// contact: "NC" — норма = замкнут (обрыв = авария), "NO" — норма = разомкнут.
class BinaryIn : public SignalIn {
 public:
  BinaryIn(esphome::binary_sensor::BinarySensor *b, const char *contact)
      : b_(b), nc_(contact[0] == 'N' && contact[1] == 'C') {}
  bool ok() const override {
    const bool closed = b_->state;
    return nc_ ? closed : !closed;
  }

 private:
  esphome::binary_sensor::BinarySensor *b_;
  bool nc_;
};
inline SignalIn *in(esphome::binary_sensor::BinarySensor *b, const char *contact) {
  return new BinaryIn(b, contact);
}
#endif

#ifdef USE_NUMBER
// Аналоговый выход через числовую сущность (регистр модуля Modbus с обратным чтением):
// доля 0…1 → 0…full. Если прочитанное значение расходится с заданием (модуль перезагрузился,
// ушёл в безопасное состояние при потере связи), запись повторяется, но не чаще раза в retry_ms.
class NumberOut : public AnalogOut {
 public:
  NumberOut(esphome::number::Number *n, float full, uint32_t retry_ms) : n_(n), full_(full), retry_ms_(retry_ms) {}
  void set(float f) override {
    f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
    want_ = f;
    const float target = f * full_;
    const bool differs = std::isnan(n_->state) || std::fabs(n_->state - target) > 0.5f;
    const uint32_t now = esphome::millis();
    if (differs && (!sent_ || target != last_target_ || now - last_ms_ >= retry_ms_)) {
      auto call = n_->make_call();
      call.set_value(target);
      call.perform();
      sent_ = true;
      last_target_ = target;
      last_ms_ = now;
    }
  }
  float get() const override { return want_; }

 private:
  esphome::number::Number *n_;
  float full_;
  uint32_t retry_ms_;
  float want_{0.0f};
  float last_target_{0.0f};
  uint32_t last_ms_{0};
  bool sent_{false};
};
inline AnalogOut *nout(esphome::number::Number *n, float full, uint32_t retry_ms = 1000) {
  return new NumberOut(n, full, retry_ms);
}

class NumberIn : public AnalogIn {
 public:
  explicit NumberIn(esphome::number::Number *n) : n_(n) {}
  float value() const override { return n_->state; }

 private:
  esphome::number::Number *n_;
};
inline AnalogIn *ain(esphome::number::Number *n) { return new NumberIn(n); }
#endif

#ifdef USE_SENSOR
class SensorIn : public AnalogIn {
 public:
  explicit SensorIn(esphome::sensor::Sensor *s) : s_(s) {}
  float value() const override { return s_->state; }

 private:
  esphome::sensor::Sensor *s_;
};
inline AnalogIn *ain(esphome::sensor::Sensor *s) { return new SensorIn(s); }
#endif

}  // namespace esph
}  // namespace vent
#endif  // USE_ESP32
