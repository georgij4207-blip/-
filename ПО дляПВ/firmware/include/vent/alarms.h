#pragma once
// Аварии, предупреждения и журнал событий. Общая таблица для системы и всех драйверов:
// новый драйвер с новой аварией добавляет код в конец перечисления.
//
// Каждая авария: текст, сброс (ручной/авто), действие. Ручные (latched) аварии
// остаются взведёнными после пропадания причины до нажатия «Сброс», и только если
// причины уже нет. Взведённые аварии и журнал сохраняются во флеш (через прошивку).

#include <cstdint>
#include <cstdio>
#include <ctime>

namespace vent {

// Коды не переставлять: маска взведённых аварий хранится во флеше по номерам битов.
enum AlarmCode : uint8_t {
  AL_NONE = 0,
  AL_FIRE,                // X8 пожарная сигнализация
  AL_THERMOSTAT,          // X1 аварийный термостат ТЭНа (+100 °C)
  AL_OVERHEAT,            // программный перегрев притока
  AL_NO_FLOW,             // X7 нет потока притока
  AL_FROST,               // обмерзание: полный стоп
  AL_SENSOR_OUTDOOR,
  AL_SENSOR_SUPPLY,
  AL_SENSOR_EXHAUST,
  AL_SENSOR_EXTRACT,
  AL_OUTDOOR_LOW,         // t_outdoor ниже допустимого
  AL_EXT_STOP,            // X4 внешний стоп
  AL_FILTER_SUPPLY,       // X2
  AL_FILTER_EXHAUST,      // X3
  AL_HEAT_INSUFFICIENT,   // ТЭН долго на максимуме, уставка не достигается
  AL_THERMOSTAT50,        // X5 термостат +50 °C (опционально)
  AL_PREHEAT_THERMOSTAT,  // X6 термостат преднагревателя (опционально)
  AL_FROST_EXHAUST_ONLY,  // обмерзание: приток остановлен, оттайка вытяжкой
  AL_FROST_REDUCED,       // обмерзание: приток снижен
  AL_FLOW_STUCK,          // реле давления замкнуто при остановленном притоке
  AL_IO_FAULT,            // нет связи с PCF8574 (входы/реле)
  AL_HEAT_UNCONTROLLED,   // приток греется при выключенном нагреве (пробит SSR?)
  AL_CONFIG,              // профиль: драйвер не задан, не реализован или не подходит к типу установки
  AL_DAMPER,              // заслонка не открылась (по концевику)
  AL_CO2_SENSOR,          // авторежим без данных CO2
  AL_COUNT
};

enum class AlarmAction : uint8_t {
  NONE,             // только сообщение
  BLOCK_HEATER,     // запрет основного ТЭНа
  BLOCK_PREHEATER,  // запрет преднагревателя
  STOP,             // нагрев выкл, продувка, стоп установки
  FIRE,             // по параметру: немедленный стоп без продувки или стоп с продувкой
  COOL,             // нагрев выкл, вентиляторы на максимум и не останавливать
};

struct AlarmDef {
  const char *text;
  bool latched;     // сброс только вручную
  bool fault;       // авария (снимает «норму» K8); false — предупреждение/состояние
  AlarmAction action;
};

inline const AlarmDef &alarm_def(uint8_t code) {
  static const AlarmDef defs[AL_COUNT] = {
      {"нет", false, false, AlarmAction::NONE},
      {"Пожарная сигнализация", true, true, AlarmAction::FIRE},
      {"Сработал аварийный термостат ТЭНа", true, true, AlarmAction::STOP},
      {"Перегрев притока", true, true, AlarmAction::STOP},
      {"Нет потока притока", true, true, AlarmAction::STOP},
      {"Обмерзание рекуператора, установка остановлена", true, true, AlarmAction::STOP},
      {"Неисправен датчик наружного воздуха", false, true, AlarmAction::STOP},
      {"Неисправен датчик притока", false, true, AlarmAction::STOP},
      {"Неисправен датчик выброса", false, true, AlarmAction::STOP},
      {"Неисправен датчик вытяжки из помещения", false, false, AlarmAction::NONE},
      {"Слишком низкая наружная температура", false, true, AlarmAction::STOP},
      {"Внешний стоп", false, false, AlarmAction::STOP},
      {"Засорён фильтр притока", false, false, AlarmAction::NONE},
      {"Засорён фильтр вытяжки", false, false, AlarmAction::NONE},
      {"Не хватает мощности ТЭНа: уставка не достигается", false, false, AlarmAction::NONE},
      {"Сработал термостат ТЭНа +50 °C", false, false, AlarmAction::BLOCK_HEATER},
      {"Сработал термостат преднагревателя", false, false, AlarmAction::BLOCK_PREHEATER},
      {"Обмерзание: приток остановлен, оттайка вытяжкой", false, false, AlarmAction::NONE},
      {"Обмерзание: баланс смещён (приток снижен/вытяжка усилена)", false, false, AlarmAction::NONE},
      {"Реле давления замкнуто при остановленном притоке", false, false, AlarmAction::NONE},
      {"Нет связи с платой входов/реле (I2C)", false, true, AlarmAction::STOP},
      {"Нагрев не управляется (пробит SSR?): охлаждение вентиляторами", true, true, AlarmAction::COOL},
      {"Ошибка конфигурации: драйвер не задан или не реализован", false, true, AlarmAction::STOP},
      {"Заслонка не открылась", true, true, AlarmAction::STOP},
      {"Нет данных CO2: авторежим на резервной ступени", false, false, AlarmAction::NONE},
  };
  return defs[code < AL_COUNT ? code : 0];
}

// Короткие названия для экрана 128×64 (≈ 16 символов).
inline const char *alarm_short_text(uint8_t code) {
  static const char *const s[AL_COUNT] = {
      "нет",
      "Пожар",
      "Термостат ТЭНа",
      "Перегрев притока",
      "Нет потока",
      "Обмерзание: стоп",
      "Датчик улицы",
      "Датчик притока",
      "Датчик выброса",
      "Датчик вытяжки",
      "Сильный мороз",
      "Внешний стоп",
      "Фильтр притока",
      "Фильтр вытяжки",
      "Мало мощн. ТЭНа",
      "Термостат +50",
      "Терм. преднагр.",
      "Оттайка вытяжкой",
      "Обмерз.: баланс",
      "Реле давл. залип",
      "Нет связи I2C",
      "ТЭН без команды",
      "Ошибка профиля",
      "Заслонка",
      "Нет данных CO2",
  };
  return s[code < AL_COUNT ? code : 0];
}

enum JournalEvent : uint8_t { EV_RAISED = 1, EV_CLEARED = 2, EV_RESET = 3 };

struct JournalEntry {
  uint32_t unix_time;  // 0 — время по NTP неизвестно
  uint32_t uptime_s;
  uint8_t code;
  uint8_t event;
};

static const int JOURNAL_SIZE = 20;
// Хранение во флеше: 1 слово — счётчик, далее по 3 слова на запись.
static const int JOURNAL_WORDS = 1 + JOURNAL_SIZE * 3;

class Alarms {
 public:
  // Состояние причины. Возвращает true, если что-то изменилось (для журнала и флеша).
  void set(uint8_t code, bool condition, uint32_t uptime_s, uint32_t unix_time) {
    const uint32_t bit = 1u << code;
    const bool was = active_ & bit;
    if (condition && !was) {
      active_ |= bit;
      if (alarm_def(code).latched)
        latched_ |= bit;
      log_(code, EV_RAISED, uptime_s, unix_time);
    } else if (!condition && was) {
      active_ &= ~bit;
      log_(code, EV_CLEARED, uptime_s, unix_time);
    }
  }

  // Ручной сброс: снимаются взведённые аварии, причины которых уже нет.
  void reset(uint32_t uptime_s, uint32_t unix_time) {
    const uint32_t cleared = latched_ & ~active_;
    for (uint8_t c = 1; c < AL_COUNT; c++)
      if (cleared & (1u << c))
        log_(c, EV_RESET, uptime_s, unix_time);
    latched_ &= active_;
    if (cleared)
      dirty_ = true;
  }

  bool on(uint8_t code) const { return (active_ | latched_) & (1u << code); }
  // Причины уже нет, авария ждёт ручного сброса.
  bool waiting_reset(uint8_t code) const { return (latched_ & ~active_) & (1u << code); }
  uint32_t on_mask() const { return active_ | latched_; }
  uint32_t latched_mask() const { return latched_; }
  bool any_latched() const { return latched_ != 0; }

  bool any_action(AlarmAction a) const {
    for (uint8_t c = 1; c < AL_COUNT; c++)
      if (on(c) && alarm_def(c).action == a)
        return true;
    return false;
  }
  bool any_fault() const {
    for (uint8_t c = 1; c < AL_COUNT; c++)
      if (on(c) && alarm_def(c).fault)
        return true;
    return false;
  }

  // Самая важная из включённых: сначала аварии, потом предупреждения; среди них меньший код.
  uint8_t top() const {
    for (uint8_t c = 1; c < AL_COUNT; c++)
      if (on(c) && alarm_def(c).fault)
        return c;
    for (uint8_t c = 1; c < AL_COUNT; c++)
      if (on(c))
        return c;
    return AL_NONE;
  }

  // --- журнал ---
  int journal_count() const { return count_ < JOURNAL_SIZE ? count_ : JOURNAL_SIZE; }
  // i = 0 — самая свежая запись
  const JournalEntry &journal(int i) const {
    const int idx = ((int) (count_ % JOURNAL_SIZE) - 1 - i + 2 * JOURNAL_SIZE) % JOURNAL_SIZE;
    return ring_[idx];
  }
  uint32_t journal_total() const { return count_; }

  // --- сохранение во флеш ---
  bool take_dirty() {
    const bool d = dirty_;
    dirty_ = false;
    return d;
  }
  void export_journal(uint32_t *words) const {
    words[0] = count_;
    for (int i = 0; i < JOURNAL_SIZE; i++) {
      words[1 + i * 3] = ring_[i].unix_time;
      words[2 + i * 3] = ring_[i].uptime_s;
      words[3 + i * 3] = (uint32_t) ring_[i].code | ((uint32_t) ring_[i].event << 8);
    }
  }
  void import(const uint32_t *words, uint32_t latched) {
    count_ = words[0];
    for (int i = 0; i < JOURNAL_SIZE; i++) {
      ring_[i].unix_time = words[1 + i * 3];
      ring_[i].uptime_s = words[2 + i * 3];
      ring_[i].code = words[3 + i * 3] & 0xFF;
      ring_[i].event = (words[3 + i * 3] >> 8) & 0xFF;
    }
    // взведённые до перезагрузки аварии остаются до ручного сброса
    latched_ = latched & ((1u << AL_COUNT) - 1);
    for (uint8_t c = 1; c < AL_COUNT; c++)
      if (!alarm_def(c).latched)
        latched_ &= ~(1u << c);
  }

 private:
  void log_(uint8_t code, uint8_t event, uint32_t uptime_s, uint32_t unix_time) {
    JournalEntry &e = ring_[count_ % JOURNAL_SIZE];
    e.unix_time = unix_time;
    e.uptime_s = uptime_s;
    e.code = code;
    e.event = event;
    count_++;
    dirty_ = true;
  }

  uint32_t active_{0};
  uint32_t latched_{0};
  JournalEntry ring_[JOURNAL_SIZE]{};
  uint32_t count_{0};
  bool dirty_{false};
};

inline const char *journal_event_text(uint8_t ev) {
  switch (ev) {
    case EV_RAISED: return "возникла";
    case EV_CLEARED: return "причина устранена";
    case EV_RESET: return "сброшена";
    default: return "?";
  }
}

// «2026-10-30 21:15:03 Перегрев притока — возникла» или «[+3605 с] …» без NTP.
inline void format_journal_entry(const JournalEntry &e, char *buf, size_t n) {
  char when[32];
  if (e.unix_time != 0) {
    time_t t = (time_t) e.unix_time;
    struct tm tm_local;
    localtime_r(&t, &tm_local);
    strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tm_local);
  } else {
    snprintf(when, sizeof(when), "[+%lu с]", (unsigned long) e.uptime_s);
  }
  snprintf(buf, n, "%s %s — %s", when, alarm_def(e.code).text, journal_event_text(e.event));
}

}  // namespace vent
