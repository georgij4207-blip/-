#pragma once
// Каналы ввода-вывода. Драйверы работают только через них:
//  - в прошивке — обёртки над сущностями ESPHome (esphome_hal.h);
//  - в хост-тестах и симуляции — подделки.
// Так драйвер не знает, на каком реле/выводе/Modbus-регистре он живёт.

#include <cstdint>

namespace vent {

class DigitalOut {  // реле, SSR вкл/выкл, сухой контакт
 public:
  virtual ~DigitalOut() = default;
  virtual void set(bool on) = 0;
  virtual bool get() const = 0;
};

class AnalogOut {  // 0–10 В, ШИМ, slow_pwm SSR: доля 0…1
 public:
  virtual ~AnalogOut() = default;
  virtual void set(float fraction) = 0;
  virtual float get() const = 0;
};

class SignalIn {  // дискретный вход; тип контакта NC/NO учтён в обёртке
 public:
  virtual ~SignalIn() = default;
  virtual bool ok() const = 0;  // true — норма (поток есть, термостат замкнут…)
};

class AnalogIn {  // измерение или настраиваемый параметр; NAN — нет данных
 public:
  virtual ~AnalogIn() = default;
  virtual float value() const = 0;
};

// Куда драйвер сообщает о своих авариях (коды — alarms.h).
class AlarmSink {
 public:
  virtual ~AlarmSink() = default;
  virtual void set(uint8_t code, bool condition) = 0;
};

}  // namespace vent
