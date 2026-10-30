// C-обёртка над firmware/include/vent_system.h для симуляции на Python (ctypes).
// Логика — та же, что в прошивке, без изменений.
// Сборка: g++ -std=c++17 -O2 -shared -fPIC -I../firmware/include vent_capi.cpp -o libvent.so

#include <cstring>

#include "vent_system.h"

using namespace vent;

extern "C" {

struct VsOut {
  int supply_mask, exhaust_mask, damper, preheater, ok;
  float heater_pct;
  int fan_state, supply_speed, exhaust_speed, frost_level;
  unsigned alarm_mask, latched_mask;
};

struct VsIn {
  int mode, exhaust_offset;
  float t_outdoor, t_supply, t_extract, t_exhaust;
  int fire_ok, thermostat_ok, thermostat50_ok, preheat_thermostat_ok, flow_ok, ext_stop_ok;
  int filter_supply_ok, filter_exhaust_ok, io_ok, heating_enabled;
  float setpoint, kp, ti_s;
};

// Параметры, которые меняет симуляция; остальные — значения по умолчанию прошивки.
struct VsCfg {
  int exhaust_only_forbidden, fire_purge, damper_enabled;
};

void *vs_new(const VsCfg *c) {
  auto *s = new VentSystem();
  VentConfig cfg;
  cfg.exhaust_only_forbidden = c->exhaust_only_forbidden;
  cfg.fire_purge = c->fire_purge;
  cfg.fan.damper_enabled = c->damper_enabled;
  s->configure(cfg);
  return s;
}

void vs_free(void *h) { delete static_cast<VentSystem *>(h); }

void vs_update(void *h, unsigned now_ms, const VsIn *i, VsOut *o) {
  auto *s = static_cast<VentSystem *>(h);
  VentInputs in;
  in.mode = (uint8_t) i->mode;
  in.exhaust_offset = i->exhaust_offset;
  in.t_outdoor = i->t_outdoor;
  in.t_supply = i->t_supply;
  in.t_extract = i->t_extract;
  in.t_exhaust = i->t_exhaust;
  in.fire_ok = i->fire_ok;
  in.thermostat_ok = i->thermostat_ok;
  in.thermostat50_ok = i->thermostat50_ok;
  in.preheat_thermostat_ok = i->preheat_thermostat_ok;
  in.flow_ok = i->flow_ok;
  in.ext_stop_ok = i->ext_stop_ok;
  in.filter_supply_ok = i->filter_supply_ok;
  in.filter_exhaust_ok = i->filter_exhaust_ok;
  in.io_ok = i->io_ok;
  in.heating_enabled = i->heating_enabled;
  in.setpoint = i->setpoint;
  in.kp = i->kp;
  in.ti_s = i->ti_s;
  const VentOutputs &r = s->update(now_ms, in);
  o->supply_mask = r.supply_mask;
  o->exhaust_mask = r.exhaust_mask;
  o->damper = r.damper;
  o->preheater = r.preheater;
  o->ok = r.ok;
  o->heater_pct = r.heater_pct;
  o->fan_state = (int) s->fans().state();
  o->supply_speed = s->fans().supply_speed();
  o->exhaust_speed = s->fans().exhaust_speed();
  o->frost_level = s->frost_level();
  o->alarm_mask = s->alarms().on_mask();
  o->latched_mask = s->alarms().latched_mask();
}

void vs_reset_alarms(void *h, unsigned now_ms) { static_cast<VentSystem *>(h)->reset_alarms(now_ms, 0); }

void vs_status(void *h, char *buf, int n) { static_cast<VentSystem *>(h)->status_text(buf, n); }

// Сохранение «во флеш» и восстановление после перезагрузки.
void vs_export(void *h, unsigned *words, unsigned *latched) {
  auto *s = static_cast<VentSystem *>(h);
  s->alarms().export_journal(words);
  *latched = s->alarms().latched_mask();
}
void vs_import(void *h, const unsigned *words, unsigned latched) {
  static_cast<VentSystem *>(h)->alarms().import(words, latched);
}

int vs_journal_words() { return JOURNAL_WORDS; }
int vs_alarm_count() { return AL_COUNT; }
const char *vs_alarm_text(int code) { return alarm_def(code).text; }
int vs_alarm_is_fault(int code) { return alarm_def(code).fault; }
const char *vs_fan_state_text(int st) { return fan_state_text((FanState) st); }
}
