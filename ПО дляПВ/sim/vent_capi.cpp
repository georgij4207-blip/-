// C-обёртка над логикой прошивки для симуляции на Python (ctypes).
// Профиль Salda — те же драйверы, что в прошивке (tests/salda_profile.h), выходы — подделки,
// которые читает тепловая модель.
// Сборка: g++ -std=c++17 -O2 -shared -fPIC -I../firmware/include -I../tests vent_capi.cpp -o libvent.so

#include <cstring>

#include "salda_profile.h"

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
  SaldaOptions o;
  o.recup.forbid_imbalance = c->exhaust_only_forbidden;
  o.sys.fire_purge = c->fire_purge;
  o.damper = c->damper_enabled;
  return new SaldaRig(o);
}

void vs_free(void *h) { delete static_cast<SaldaRig *>(h); }

void vs_update(void *h, unsigned now_ms, const VsIn *i, VsOut *o) {
  auto *r = static_cast<SaldaRig *>(h);
  VentInputs in;
  in.mode = (uint8_t) i->mode;
  in.exhaust_offset = i->exhaust_offset;
  in.t_outdoor = i->t_outdoor;
  in.t_supply = i->t_supply;
  in.t_extract = i->t_extract;
  in.t_exhaust = i->t_exhaust;
  in.fire_ok = i->fire_ok;
  in.flow_ok = i->flow_ok;
  in.ext_stop_ok = i->ext_stop_ok;
  in.filter_supply_ok = i->filter_supply_ok;
  in.filter_exhaust_ok = i->filter_exhaust_ok;
  in.io_ok = i->io_ok;
  in.heating_enabled = i->heating_enabled;
  in.setpoint = i->setpoint;
  r->th100.v = i->thermostat_ok;
  r->th50.v = i->thermostat50_ok;
  r->preheat_th.v = i->preheat_thermostat_ok;
  r->kp.v = i->kp;
  r->ti.v = i->ti_s;
  const VentStatus &st = r->sys.update(now_ms, in);
  // выходы — то, что драйверы реально записали в «реле» и «SSR»
  o->supply_mask = r->supply_mask();
  o->exhaust_mask = r->exhaust_mask();
  o->damper = r->k[6].v;
  o->preheater = r->ssr_preheater.v;
  o->ok = r->k[7].v;
  o->heater_pct = r->heater_pct();
  o->fan_state = (int) r->sys.fans().state();
  o->supply_speed = st.supply_level;
  o->exhaust_speed = st.exhaust_level;
  o->frost_level = r->sys.frost_level();
  o->alarm_mask = r->sys.alarms().on_mask();
  o->latched_mask = r->sys.alarms().latched_mask();
}

void vs_reset_alarms(void *h, unsigned now_ms) { static_cast<SaldaRig *>(h)->sys.reset_alarms(now_ms, 0); }

void vs_status(void *h, char *buf, int n) { static_cast<SaldaRig *>(h)->sys.status_text(buf, n); }

// Сохранение «во флеш» и восстановление после перезагрузки.
void vs_export(void *h, unsigned *words, unsigned *latched) {
  auto *s = static_cast<SaldaRig *>(h);
  s->sys.alarms().export_journal(words);
  *latched = s->sys.alarms().latched_mask();
}
void vs_import(void *h, const unsigned *words, unsigned latched) {
  static_cast<SaldaRig *>(h)->sys.alarms().import(words, latched);
}

int vs_journal_words() { return JOURNAL_WORDS; }
int vs_alarm_count() { return AL_COUNT; }
const char *vs_alarm_text(int code) { return alarm_def(code).text; }
int vs_alarm_is_fault(int code) { return alarm_def(code).fault; }
const char *vs_fan_state_text(int st) { return fan_state_text((FanState) st); }
}
