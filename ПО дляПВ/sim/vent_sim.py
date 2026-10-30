"""Тепловая модель Salda RIS 400P + настоящая логика прошивки (libvent.so через ctypes).

Модель — оценочная, по паспортным цифрам и допущениям из docs/simulation.md.
Её задача — проверить устойчивость логики при разбросе параметров, а не предсказать
точные температуры.
"""

from __future__ import annotations

import ctypes
import math
import os
import subprocess
from dataclasses import dataclass, field

HERE = os.path.dirname(os.path.abspath(__file__))
FIRMWARE_INCLUDE = os.path.join(HERE, "..", "firmware", "include")

RHO_C = 1.2 * 1005.0 / 3600.0  # Вт/(К·м³/ч): теплоёмкость потока воздуха

# Коды аварий (совпадают с vent_alarms.h, проверяется при загрузке)
AL = dict(FIRE=1, THERMOSTAT=2, OVERHEAT=3, NO_FLOW=4, FROST=5, SENSOR_OUTDOOR=6,
          SENSOR_SUPPLY=7, SENSOR_EXHAUST=8, SENSOR_EXTRACT=9, OUTDOOR_LOW=10,
          EXT_STOP=11, FILTER_SUPPLY=12, FILTER_EXHAUST=13, HEAT_INSUFFICIENT=14,
          THERMOSTAT50=15, PREHEAT_THERMOSTAT=16, FROST_EXHAUST_ONLY=17,
          FROST_REDUCED=18, FLOW_STUCK=19, IO_FAULT=20, HEAT_UNCONTROLLED=21)


# --------------------------------------------------------------------------- логика

class _VsOut(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int) for n in
                ("supply_mask", "exhaust_mask", "damper", "preheater", "ok")] + [
        ("heater_pct", ctypes.c_float)] + [(n, ctypes.c_int) for n in
                ("fan_state", "supply_speed", "exhaust_speed", "frost_level")] + [
        ("alarm_mask", ctypes.c_uint), ("latched_mask", ctypes.c_uint)]


class _VsIn(ctypes.Structure):
    _fields_ = [("mode", ctypes.c_int), ("exhaust_offset", ctypes.c_int)] + [
        (n, ctypes.c_float) for n in ("t_outdoor", "t_supply", "t_extract", "t_exhaust")] + [
        (n, ctypes.c_int) for n in (
            "fire_ok", "thermostat_ok", "thermostat50_ok", "preheat_thermostat_ok", "flow_ok",
            "ext_stop_ok", "filter_supply_ok", "filter_exhaust_ok", "io_ok", "heating_enabled")] + [
        (n, ctypes.c_float) for n in ("setpoint", "kp", "ti_s")]


class _VsCfg(ctypes.Structure):
    _fields_ = [(n, ctypes.c_int) for n in ("exhaust_only_forbidden", "fire_purge", "damper_enabled")]


def load_lib() -> ctypes.CDLL:
    so = os.path.join(HERE, "libvent.so")
    src = [os.path.join(HERE, "vent_capi.cpp")] + [
        os.path.join(FIRMWARE_INCLUDE, f) for f in os.listdir(FIRMWARE_INCLUDE)]
    if not os.path.exists(so) or any(os.path.getmtime(s) > os.path.getmtime(so) for s in src):
        subprocess.check_call(["g++", "-std=c++17", "-O2", "-shared", "-fPIC",
                               "-I", FIRMWARE_INCLUDE, src[0], "-o", so])
    lib = ctypes.CDLL(so)
    lib.vs_new.restype = ctypes.c_void_p
    lib.vs_new.argtypes = [ctypes.POINTER(_VsCfg)]
    lib.vs_free.argtypes = [ctypes.c_void_p]
    lib.vs_update.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.POINTER(_VsIn), ctypes.POINTER(_VsOut)]
    lib.vs_reset_alarms.argtypes = [ctypes.c_void_p, ctypes.c_uint]
    lib.vs_status.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
    lib.vs_export.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint), ctypes.POINTER(ctypes.c_uint)]
    lib.vs_import.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint), ctypes.c_uint]
    lib.vs_alarm_text.restype = ctypes.c_char_p
    lib.vs_fan_state_text.restype = ctypes.c_char_p
    assert lib.vs_alarm_count() == max(AL.values()) + 1, "коды аварий в vent_sim.py устарели"
    return lib


LIB = load_lib()


def alarm_text(code: int) -> str:
    return LIB.vs_alarm_text(code).decode()


def fan_state_text(st: int) -> str:
    return LIB.vs_fan_state_text(st).decode()


class Controller:
    """Экземпляр логики прошивки. Перезагрузка = новый экземпляр + восстановление «флеша»."""

    def __init__(self, exhaust_only_forbidden=False, fire_purge=False, damper_enabled=True):
        self._cfg = _VsCfg(int(exhaust_only_forbidden), int(fire_purge), int(damper_enabled))
        self.h = LIB.vs_new(ctypes.byref(self._cfg))
        self.out = _VsOut()
        self.inp = _VsIn()
        self.inp.mode, self.inp.setpoint, self.inp.kp, self.inp.ti_s = 2, 18.0, 10.0, 120.0
        for n in ("fire_ok", "thermostat_ok", "thermostat50_ok", "preheat_thermostat_ok",
                  "ext_stop_ok", "filter_supply_ok", "filter_exhaust_ok", "io_ok", "heating_enabled"):
            setattr(self.inp, n, 1)

    def __del__(self):
        if getattr(self, "h", None):
            LIB.vs_free(self.h)

    def update(self, now_ms: int) -> _VsOut:
        LIB.vs_update(self.h, now_ms, ctypes.byref(self.inp), ctypes.byref(self.out))
        return self.out

    def reset_alarms(self, now_ms: int):
        LIB.vs_reset_alarms(self.h, now_ms)

    def status(self) -> str:
        buf = ctypes.create_string_buffer(200)
        LIB.vs_status(self.h, buf, 200)
        return buf.value.decode()

    def save_flash(self):
        n = LIB.vs_journal_words()
        words = (ctypes.c_uint * n)()
        latched = ctypes.c_uint()
        LIB.vs_export(self.h, words, ctypes.byref(latched))
        return words, latched.value

    def load_flash(self, flash):
        words, latched = flash
        LIB.vs_import(self.h, words, latched)


# --------------------------------------------------------------------------- модель

@dataclass
class Plant:
    """Параметры установки и помещения (оценки, см. docs/simulation.md)."""
    eta0: float = 0.55            # КПД рекуператора по притоку при равных расходах
    flow_factor: float = 0.75     # расход с сетью / без сети
    q_max: float = 400.0          # м³/ч на 3-й ступени без сети
    speed_frac: tuple = (0.0, 0.45, 0.70, 1.00)
    t_room: float = 22.0
    heater_w: float = 1500.0
    preheater_w: float = 600.0
    flow_switch_m3h: float = 60.0  # уставка реле перепада давления (≈ расход)
    frost_onset_c: float = 0.0     # t выброса, ниже которой растёт лёд (влажная вытяжка)
    k_ice: float = 5.4e-5          # рост льда: 1/(с·К) на долю расхода
    k_melt: float = 8.3e-5         # таяние: 1/(с·К)
    hw_flow_interlock: bool = True  # цепь +12V_FLOW (ТЭНы без потока не включаются)


@dataclass
class Faults:
    supply_fan_dead_at: float | None = None   # с: мотор притока перестал вращаться
    sensor_open_at: dict = field(default_factory=dict)  # {"t_supply": 3600}
    ssr_welded_at: float | None = None        # с: SSR ТЭНа «залип» (пробит)
    preheater_dead: bool = False
    power_loss: tuple | None = None           # (с, длительность с)
    fire_at: tuple | None = None              # (начало, конец) сигнала пожара


class Simulation:
    def __init__(self, plant: Plant, t_out, mode=2, faults: Faults | None = None,
                 dt=1.0, controller_kwargs=None, setpoint=18.0):
        self.p = plant
        self.t_out_fn = t_out if callable(t_out) else (lambda t, v=t_out: v)
        self.mode = mode
        self.f = faults or Faults()
        self.dt = dt
        self.ckw = controller_kwargs or {}
        self.setpoint = setpoint
        self.ctrl = Controller(**self.ckw)
        self.ctrl.inp.mode = mode
        self.ctrl.inp.setpoint = setpoint
        self.boot_t = 0.0
        self.powered = True
        self.flash = None
        # состояние модели
        t0 = self.t_out_fn(0.0)
        self.q_s = self.q_e = 0.0
        self.p_pre = 0.0
        self.t_rec_sup = (t0 + plant.t_room) / 2
        self.t_exh = (t0 + plant.t_room) / 2
        self.t_el = self.t_rec_sup
        self.t_sens = self.t_rec_sup
        self.ice = 0.0
        self.th100_tripped = False
        self.th50_open = False
        self.log: list[dict] = []
        self.events: list[tuple[float, str]] = []
        self._prev_mask = 0
        self.violations: list[str] = []

    # --- один шаг модели (физика) ---
    def _plant_step(self, t, o_sup_spd, o_exh_spd, heater_pct, preheater_on):
        p, dt = self.p, self.dt
        t_out = self.t_out_fn(t)
        fan_dead = self.f.supply_fan_dead_at is not None and t >= self.f.supply_fan_dead_at
        qs_t = 0.0 if fan_dead else p.q_max * p.speed_frac[o_sup_spd] * p.flow_factor
        qe_t = p.q_max * p.speed_frac[o_exh_spd] * p.flow_factor * (1.0 - 0.5 * self.ice)
        self.q_s += (qs_t - self.q_s) * min(1.0, dt / 8.0)
        self.q_e += (qe_t - self.q_e) * min(1.0, dt / 8.0)
        qs, qe = self.q_s, self.q_e

        flow_switch = qs > p.flow_switch_m3h
        hw_ok = flow_switch or not p.hw_flow_interlock

        # преднагреватель (перед рекуператором)
        pre_target = p.preheater_w if (preheater_on and hw_ok and not self.f.preheater_dead) else 0.0
        self.p_pre += (pre_target - self.p_pre) * min(1.0, dt / 30.0)
        t_pre = t_out + (self.p_pre / (RHO_C * qs) if qs > 5 else 0.0)

        # рекуператор
        if qs > 5 and qe > 5:
            eta = p.eta0 * (1.0 - 0.4 * self.ice) * min(qe / qs, 1.3) ** 0.5
            eta = min(eta, 0.9)
            rec_t = t_pre + eta * (p.t_room - t_pre)
            exh_t = p.t_room - (qs / qe) * (rec_t - t_pre)
        elif qe > 5:          # только вытяжка: пластины прогреваются воздухом помещения
            rec_t = self.t_rec_sup
            exh_t = p.t_room - 2.0
        else:                 # нет потока: медленный дрейф к середине
            rec_t = exh_t = (t_out + p.t_room) / 2
        tau_rec = 180.0 if (qs > 5 or qe > 5) else 1800.0
        self.t_rec_sup += (rec_t - self.t_rec_sup) * min(1.0, dt / tau_rec)
        self.t_exh += (exh_t - self.t_exh) * min(1.0, dt / tau_rec)

        # обмерзание со стороны вытяжки
        if self.t_exh < p.frost_onset_c and qe > 5:
            self.ice += p.k_ice * (p.frost_onset_c - self.t_exh) * (qe / p.q_max) * dt
        elif self.t_exh > 0.5:
            self.ice -= p.k_melt * self.t_exh * dt
        self.ice = min(max(self.ice, 0.0), 1.0)

        # ТЭН: термостаты в силовой цепи
        welded = self.f.ssr_welded_at is not None and t >= self.f.ssr_welded_at
        duty = 1.0 if welded else (heater_pct / 100.0 if hw_ok else 0.0)
        power_chain = not self.th100_tripped and not self.th50_open
        p_el = p.heater_w * duty if power_chain else 0.0
        ua_flow = 0.026 * qs
        ua = 1.0 + ua_flow
        c_el = 1000.0
        t_in = self.t_rec_sup
        self.t_el += (p_el - ua * (self.t_el - t_in)) / c_el * dt
        air_out = t_in + (ua_flow * (self.t_el - t_in) / (RHO_C * qs) if qs > 5 else 0.0)
        k_th = 0.05 + 0.30 * math.exp(-qs / 50.0)
        t_th = air_out + k_th * (self.t_el - air_out)
        if t_th > 100.0:
            self.th100_tripped = True       # ручной сброс, остаётся до конца прогона
        if t_th > 50.0:
            self.th50_open = True
        elif t_th < 40.0:
            self.th50_open = False
        if qs > 20:
            sens_t = air_out + 0.01 * (self.t_el - air_out)
            tau_s = 15.0
        else:
            sens_t = t_in + 0.15 * (self.t_el - t_in)
            tau_s = 60.0
        self.t_sens += (sens_t - self.t_sens) * min(1.0, dt / tau_s)
        return t_out, flow_switch, p_el

    # --- прогон ---
    def run(self, duration_s, actions=None):
        """actions: {время_с: функция(sim)} — вмешательства (сброс, смена режима...)."""
        actions = dict(actions or {})
        out = None
        steps = int(duration_s / self.dt)
        sup_spd = exh_spd = 0
        heater = 0.0
        pre = False
        for k in range(steps):
            t = k * self.dt
            for ta in [a for a in actions if a <= t]:
                actions.pop(ta)(self)

            # пропадание питания: контроллер выключен, при возврате — новый экземпляр
            if self.f.power_loss:
                pl0, pld = self.f.power_loss
                if self.powered and pl0 <= t < pl0 + pld:
                    self.powered = False
                    self.flash = self.ctrl.save_flash()
                    self.events.append((t, "питание пропало"))
                elif not self.powered and t >= pl0 + pld:
                    self.powered = True
                    self.ctrl = Controller(**self.ckw)
                    self.ctrl.inp.mode = self.mode
                    self.ctrl.inp.setpoint = self.setpoint
                    self.ctrl.load_flash(self.flash)
                    self.boot_t = t
                    self.events.append((t, "питание вернулось, загрузка"))

            t_out, flow_switch, p_el = self._plant_step(t, sup_spd, exh_spd, heater, pre)

            if self.powered:
                i = self.ctrl.inp
                i.t_outdoor = t_out
                i.t_supply = self.t_sens
                i.t_extract = self.p.t_room
                i.t_exhaust = self.t_exh
                for name, t0 in self.f.sensor_open_at.items():
                    if t >= t0:
                        setattr(i, name, float("nan"))
                i.flow_ok = int(flow_switch)
                i.thermostat_ok = int(not self.th100_tripped)
                i.fire_ok = int(not (self.f.fire_at and self.f.fire_at[0] <= t < self.f.fire_at[1]))
                out = self.ctrl.update(int((t - self.boot_t) * 1000))
                sup_spd = _speed(out.supply_mask)
                exh_spd = _speed(out.exhaust_mask)
                heater, pre = out.heater_pct, bool(out.preheater)
                self._check_invariants(t, out)
                if out.alarm_mask != self._prev_mask:
                    new = out.alarm_mask & ~self._prev_mask
                    for c in range(1, 32):
                        if new & (1 << c):
                            self.events.append((t, "авария/предупр.: " + alarm_text(c)))
                    self._prev_mask = out.alarm_mask
            else:
                sup_spd = exh_spd = 0
                heater, pre = 0.0, False

            if k % max(1, int(10 / self.dt)) == 0:
                self.log.append(dict(
                    t=t, t_out=t_out, t_supply=self.t_sens, t_exhaust=self.t_exh,
                    t_rec=self.t_rec_sup, t_el=self.t_el, ice=self.ice,
                    q_s=self.q_s, q_e=self.q_e, heater=heater if self.powered else 0.0,
                    p_el=p_el, preheater=pre, sup=sup_spd, exh=exh_spd,
                    state=out.fan_state if (out and self.powered) else -1,
                    ok=bool(out.ok) if (out and self.powered) else False,
                    alarms=out.alarm_mask if (out and self.powered) else 0,
                    frost=out.frost_level if (out and self.powered) else 0))
        return self

    def _check_invariants(self, t, out):
        sup = _speed(out.supply_mask)
        exh = _speed(out.exhaust_mask)
        if bin(out.supply_mask).count("1") > 1 or bin(out.exhaust_mask).count("1") > 1:
            self.violations.append(f"{t:.0f} с: две ступени одного мотора")
        if (out.heater_pct > 0 or out.preheater) and (sup == 0 or exh == 0):
            self.violations.append(f"{t:.0f} с: нагрев без работающих вентиляторов")
        if out.heater_pct > 0 and math.isnan(self.ctrl.inp.t_supply):
            self.violations.append(f"{t:.0f} с: нагрев при неисправном датчике притока")


def _speed(mask: int) -> int:
    return {0: 0, 1: 1, 2: 2, 4: 3}.get(mask, 0)
