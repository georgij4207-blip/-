#pragma once
// EC 0–10 В / ШИМ — ЗАГЛУШКА: интерфейс есть, логики нет.
// План: AnalogOut 0–10 В или ШИМ, DigitalOut «разрешение», ступень → % по таблице
// профиля, плавный разгон (рампа). Контроль: тахо (AnalogIn, об/мин) — авария при
// расхождении с заданием; вход «авария EC» (SignalIn).

#include "fan.h"

namespace vent {

class EcAnalogFan : public FanStub {
 public:
  const char *type_name() const override { return "EC 0–10 В / ШИМ"; }
};

}  // namespace vent
