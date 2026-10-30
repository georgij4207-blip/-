#pragma once
// Интерфейс драйвера вентилятора. Последовательность пуска/останова (sequencer.h)
// оперирует ступенями 1…levels(); как ступень превращается в реле, 0–10 В, ШИМ
// или регистр Modbus — дело драйвера.
//
// Реализации (новый тип = новый файл fan_*.h + packages/drivers/fan_*.yaml):
//   fan_ac_taps.h        AC, отводы обмотки на реле (2–5 ступеней)        — реализован (Salda)
//   fan_ac_regulator.h   AC через трансформаторный/симисторный регулятор   — заглушка
//   fan_ec_analog.h      EC 0–10 В / ШИМ, тахо и авария                    — заглушка
//   fan_ec_modbus.h      EC по Modbus RTU (ebm-papst, Ziehl-Abegg, Rosenberg) — заглушка
//   fan_vfd.h            частотник: пуск + 0–10 В/4–20 мА или Modbus        — заглушка

#include <cstdint>

#include "hal.h"

namespace vent {

class FanDriver {
 public:
  virtual ~FanDriver() = default;
  virtual const char *type_name() const = 0;
  virtual bool implemented() const { return true; }

  virtual uint8_t levels() const = 0;            // число ступеней
  virtual void set_target(uint8_t level) = 0;    // 0 — стоп
  virtual void update(uint32_t now_ms) = 0;      // вызывается каждый цикл, пишет выходы
  virtual uint8_t active_level() const = 0;      // ступень, которая реально подана
  virtual bool settled() const = 0;              // переход на заданную ступень завершён
  // Аварии мотора: тахо, ток, авария частотника, термоконтакт обмотки.
  virtual void report(AlarmSink &) {}
};

// Общая заглушка: драйвер объявлен, но не реализован. Система с таким драйвером
// не запустится (авария «Ошибка конфигурации»), выходы не трогаются.
class FanStub : public FanDriver {
 public:
  bool implemented() const override { return false; }
  uint8_t levels() const override { return 3; }
  void set_target(uint8_t) override {}
  void update(uint32_t) override {}
  uint8_t active_level() const override { return 0; }
  bool settled() const override { return true; }
};

}  // namespace vent
