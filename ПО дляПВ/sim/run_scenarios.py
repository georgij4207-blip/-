"""Сценарии и прогон с разбросом параметров. Результат: sim/out/*.png и sim/out/results.md.

Запуск:  python3 run_scenarios.py          (нужен matplotlib для графиков)
"""

from __future__ import annotations

import itertools
import math
import os
import sys

from vent_sim import AL, Faults, Plant, Simulation, alarm_text, fan_state_text

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")
os.makedirs(OUT, exist_ok=True)

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except ImportError:  # графики необязательны
    plt = None

H = 3600.0
lines: list[str] = []


def md(s=""):
    lines.append(s)


def has(sim, code):
    return any(r["alarms"] & (1 << code) for r in sim.log)


def first_time(sim, code):
    for t, e in sim.events:
        if e.endswith(alarm_text(code)):
            return t
    return None


def window(sim, t0, t1):
    return [r for r in sim.log if t0 <= r["t"] < t1]


def mean(rows, key, cond=lambda r: True):
    v = [r[key] for r in rows if cond(r)]
    return sum(v) / len(v) if v else float("nan")


def plot(sim, name, title):
    if plt is None:
        return
    L = sim.log
    t = [r["t"] / H for r in L]
    fig, ax = plt.subplots(4, 1, figsize=(10, 9), sharex=True)
    ax[0].plot(t, [r["t_supply"] for r in L], label="t_supply (датчик)")
    ax[0].plot(t, [r["t_exhaust"] for r in L], label="t_exhaust (выброс)")
    ax[0].plot(t, [r["t_rec"] for r in L], label="после рекуператора", alpha=0.6)
    ax[0].axhline(18, color="gray", ls="--", lw=0.8)
    ax[0].axhline(0, color="c", ls=":", lw=0.8)
    ax[0].set_ylabel("°C")
    ax[0].legend(loc="best", fontsize=8)
    ax[0].set_title(title, fontsize=10)
    ax[1].plot(t, [r["heater"] for r in L], label="ТЭН, % (команда)")
    ax[1].plot(t, [r["p_el"] / 15 for r in L], label="ТЭН, % (фактически)", alpha=0.6)
    ax[1].plot(t, [100 * r["preheater"] for r in L], label="преднагрев", alpha=0.5)
    ax[1].set_ylabel("%")
    ax[1].legend(loc="best", fontsize=8)
    ax[2].step(t, [r["sup"] for r in L], label="приток, ступень", where="post")
    ax[2].step(t, [r["exh"] for r in L], label="вытяжка, ступень", where="post", alpha=0.7)
    ax[2].plot(t, [3 * r["ice"] for r in L], label="лёд (0…3 = 0…100 %)", alpha=0.7)
    ax[2].set_ylabel("ступень")
    ax[2].legend(loc="best", fontsize=8)
    ax[3].plot(t, [r["t_el"] for r in L], label="ТЭН, поверхность (модель)", color="r")
    ax[3].axhline(100, color="r", ls=":", lw=0.8)
    ax[3].set_ylabel("°C")
    ax[3].set_xlabel("часы")
    ax[3].legend(loc="best", fontsize=8)
    fig.tight_layout()
    fig.savefig(os.path.join(OUT, name + ".png"), dpi=80)
    plt.close(fig)


def events_md(sim, limit=12):
    ev = sim.events[:limit]
    for t, e in ev:
        md(f"  - {t / 60:6.1f} мин — {e}")
    if len(sim.events) > limit:
        md(f"  - … ещё {len(sim.events) - limit} событий")


def violations_md(sim):
    md(f"- Нарушения инвариантов безопасности: **{len(sim.violations) or 'нет'}**")
    for v in sim.violations[:5]:
        md(f"  - {v}")


# =========================================================================== сетка

md("# Результаты симуляции")
md()
md("Логика — настоящая `firmware/include/vent_system.h` (через ctypes). Модель — оценочная,")
md("допущения в `docs/simulation.md`. Уставка притока 18 °C, помещение 22 °C.")
md()
md("## 1. Сетка: наружная температура × ступень × КПД × расход")
md()
md("6 ч на каждую точку. Показаны последние 2 ч: средняя t притока (пока приток работает),")
md("средняя мощность ТЭНа, доля времени преднагревателя, доля времени «только вытяжка»,")
md("предупреждения/аварии. Расход: 1,00 — без сети, 0,80/0,70 — с сетью.")
md()
md("| t нар. | ступень | КПД | расход | приток, м³/ч | t притока | ТЭН | преднагр. | только вытяжка | приток/вытяжка в конце | события |")
md("|---|---|---|---|---|---|---|---|---|---|---|")

grid = []
total_viol = 0
for t_out, mode, eta, ff in itertools.product((-30, -20, -10), (1, 2, 3), (0.50, 0.55, 0.65), (0.70, 0.80, 1.00)):
    s = Simulation(Plant(eta0=eta, flow_factor=ff), t_out, mode=mode).run(6 * H)
    w = window(s, 4 * H, 6 * H)
    q = 400 * (0.45, 0.70, 1.0)[mode - 1] * ff
    t_sup = mean(w, "t_supply", lambda r: r["sup"] > 0)
    heat = mean(w, "heater")
    pre = mean(w, "preheater")
    exh_only = sum(1 for r in w if r["sup"] == 0 and r["exh"] > 0) / max(1, len(w))
    flags = []
    if has(s, AL["HEAT_INSUFFICIENT"]):
        flags.append("мощности мало")
    if has(s, AL["FROST_REDUCED"]):
        flags.append("баланс смещался")
    if has(s, AL["FROST_EXHAUST_ONLY"]):
        flags.append("оттайка вытяжкой")
    if has(s, AL["FROST"]):
        flags.append("**стоп: обмерзание**")
    if has(s, AL["OVERHEAT"]):
        flags.append("**перегрев**")
    total_viol += len(s.violations)
    grid.append(dict(t_out=t_out, mode=mode, eta=eta, ff=ff, t_sup=t_sup, heat=heat, pre=pre,
                     exh_only=exh_only, flags=flags, max_sup=max(r["t_supply"] for r in s.log)))
    md(f"| {t_out} | {mode} | {eta:.2f} | {ff:.2f} | {q:.0f} | {t_sup:.1f} | {heat:.0f} % | "
       f"{pre * 100:.0f} % | {exh_only * 100:.0f} % | {s.log[-1]['sup']}/{s.log[-1]['exh']} | {', '.join(flags) or '—'} |")

md()
md(f"Нарушений инвариантов безопасности во всей сетке ({len(grid)} прогонов): **{total_viol or 'нет'}**.")
md(f"Максимальная температура притока во всей сетке: **{max(g['max_sup'] for g in grid):.1f} °C** "
   f"(порог аварии 45 °C).")
md()

# =========================================================================== сценарии

md("## 2. −30 °C на 1-й ступени, ТЭН 1,5 кВт: крайние значения")
md()
for eta, ff in ((0.50, 0.70), (0.50, 1.00), (0.65, 0.70), (0.65, 1.00)):
    s = Simulation(Plant(eta0=eta, flow_factor=ff), -30, mode=1).run(8 * H)
    name = f"m30_s1_eta{int(eta * 100)}_ff{int(ff * 100)}"
    plot(s, name, f"−30 °C, ступень 1, КПД {eta:.2f}, расход ×{ff:.2f}")
    w = window(s, 4 * H, 8 * H)
    md(f"### КПД {eta:.2f}, расход ×{ff:.2f} ({400 * 0.45 * ff:.0f} м³/ч) — `out/{name}.png`")
    md()
    md(f"- t притока (при работающем притоке): {mean(w, 't_supply', lambda r: r['sup'] > 0):.1f} °C, "
       f"ТЭН в среднем {mean(w, 'heater'):.0f} %, максимум команды {max(r['heater'] for r in w):.0f} %")
    md(f"- После рекуператора: {mean(w, 't_rec', lambda r: r['sup'] > 0):.1f} °C, "
       f"выброс {mean(w, 't_exhaust', lambda r: r['sup'] > 0):.1f} °C, "
       f"преднагреватель {mean(w, 'preheater') * 100:.0f} % времени")
    md(f"- Доля времени «только вытяжка» (часы 4–8): "
       f"{sum(1 for r in w if r['sup'] == 0 and r['exh'] > 0) / len(w) * 100:.0f} %, лёд макс. "
       f"{max(r['ice'] for r in s.log) * 100:.0f} %")
    violations_md(s)
    md("- События:")
    events_md(s, 6)
    md()

md("### −30 °C, ступень 1, КПД 0,65, без сети, `exhaust_only_forbidden: true` (котёл/камин)")
md()
s = Simulation(Plant(eta0=0.65, flow_factor=1.0), -30, mode=1,
               controller_kwargs=dict(exhaust_only_forbidden=True)).run(3 * H)
plot(s, "m30_s1_forbidden", "−30 °C, ступень 1, КПД 0,65, без сети, вытяжка без притока запрещена")
violations_md(s)
md("- События:")
events_md(s)
md()

# --- отказ вентилятора ---
md("## 3. Отказ приточного вентилятора (−10 °C, ступень 2, через 1 ч мотор встал)")
md()
for hw in (True, False):
    s = Simulation(Plant(hw_flow_interlock=hw), -10, mode=2,
                   faults=Faults(supply_fan_dead_at=1 * H)).run(2 * H)
    name = "fan_fail_hw" if hw else "fan_fail_fw_only"
    plot(s, name, "Отказ мотора притока" + (" (цепь +12V_FLOW есть)" if hw else " (без аппаратной блокировки)"))
    w = window(s, 1 * H, 2 * H)
    md(f"### {'С аппаратной блокировкой +12V_FLOW' if hw else 'Только прошивка (без +12V_FLOW)'} — `out/{name}.png`")
    md()
    md(f"- Макс. температура ТЭНа после отказа: **{max(r['t_el'] for r in w):.0f} °C**, "
       f"датчика притока: {max(r['t_supply'] for r in w):.1f} °C")
    t_off = next((r["t"] for r in w if r["p_el"] == 0), None)
    md(f"- ТЭН обесточен через {((t_off or 0) - 1 * H):.0f} с после отказа "
       f"(шаг журнала модели 10 с)")
    md(f"- Аварийный термостат +100 °C: {'сработал' if s.th100_tripped else 'не сработал'}")
    violations_md(s)
    md("- События:")
    events_md(s)
    md()

# --- обрыв датчика ---
md("## 4. Обрыв датчика притока (−20 °C, ступень 2, через 1 ч)")
md()
s = Simulation(Plant(), -20, mode=2, faults=Faults(sensor_open_at={"t_supply": 1 * H})).run(2 * H)
plot(s, "sensor_open", "Обрыв датчика притока через 1 ч")
w = window(s, 1 * H, 2 * H)
md(f"- ТЭН после обрыва: макс. команда {max(r['heater'] for r in w if r['t'] > 1 * H + 10):.0f} %")
violations_md(s)
md("- События:")
events_md(s)
md()

# --- перегрев: пробитый SSR ---
md("## 5. Перегрев: SSR ТЭНа пробит (всегда включён), −10 °C, ступень 1, через 1 ч")
md()
s = Simulation(Plant(), -10, mode=1, faults=Faults(ssr_welded_at=1 * H)).run(2 * H)
plot(s, "ssr_welded", "Пробит SSR ТЭНа (−10 °C, ступень 1)")
w = window(s, 1 * H, 2 * H)
md(f"- Макс. t притока: {max(r['t_supply'] for r in w):.1f} °C, макс. ТЭН: **{max(r['t_el'] for r in w):.0f} °C**")
md(f"- Аварийный термостат +100 °C: {'сработал' if s.th100_tripped else 'не сработал'}")
violations_md(s)
md("- События:")
events_md(s)
md()

# --- обмерзание: преднагреватель не работает ---
md("## 6. Обмерзание: преднагреватель неисправен (−20 °C, ступень 2, КПД 0,65)")
md()
s = Simulation(Plant(eta0=0.65), -20, mode=2, faults=Faults(preheater_dead=True)).run(6 * H)
plot(s, "frost_preheater_dead", "Обмерзание: преднагреватель не греет (−20 °C, ступень 2, КПД 0,65)")
violations_md(s)
md(f"- Лёд макс.: {max(r['ice'] for r in s.log) * 100:.0f} %")
md("- События:")
events_md(s, 16)
md()

# --- пропадание питания ---
md("## 7. Пропадание питания")
md()
s = Simulation(Plant(), -20, mode=2, faults=Faults(power_loss=(1 * H, 600))).run(2 * H)
plot(s, "power_loss", "Пропадание питания на 10 мин (−20 °C, ступень 2)")
w = window(s, 1 * H + 600, 2 * H)
t_run = next((r["t"] for r in w if r["state"] == 3), None)
t_heat = next((r["t"] for r in w if r["heater"] > 0), None)
md("### 10 мин без питания во время работы с нагревом — `out/power_loss.png`")
md()
md(f"- После возврата питания: «Работа» через {(t_run - 1 * H - 600):.0f} с, "
   f"нагрев через {(t_heat - 1 * H - 600):.0f} с (загрузка 10 с + заслонка 30 с + вытяжка 3 с + 10 с)")
violations_md(s)
md("- События:")
events_md(s)
md()

s = Simulation(Plant(), -10, mode=1, faults=Faults(ssr_welded_at=1 * H, power_loss=(1.3 * H, 300))).run(2 * H)
w = window(s, 1.3 * H + 300, 2 * H)
md("### Питание пропало после аварии «Нагрев не управляется» (пробитый SSR, до сброса)")
md()
md(f"- После загрузки: приток/вытяжка в конце {w[-1]['sup']}/{w[-1]['exh']}, "
   f"нагрев {'не включался' if all(r['heater'] == 0 for r in w) else 'включался'}, "
   f"авария {'восстановлена из «флеша»' if w[-1]['alarms'] else 'потеряна'}: «{alarm_text(next((c for c in range(1, 32) if w[-1]['alarms'] & (1 << c)), 0))}»")
md("- События:")
events_md(s)
md()

# --- пожар ---
md("## 8. Пожар (X8) во время работы с нагревом, −10 °C")
md()
s = Simulation(Plant(), -10, mode=2, faults=Faults(fire_at=(1 * H, 1.5 * H))).run(2 * H,
               actions={1.6 * H: lambda sim: sim.ctrl.reset_alarms(int((1.6 * H - sim.boot_t) * 1000))})
plot(s, "fire", "Пожар через 1 ч, сигнал снят через 30 мин, сброс в 1 ч 36 мин")
w = window(s, 1 * H, 1 * H + 20)
md(f"- Через 10 с после сигнала: приток {w[-1]['sup']}, вытяжка {w[-1]['exh']}, ТЭН {w[-1]['heater']:.0f} %")
violations_md(s)
md("- События:")
events_md(s)
md()

with open(os.path.join(OUT, "results.md"), "w") as f:
    f.write("\n".join(lines) + "\n")
print("\n".join(lines))
sys.exit(1 if total_viol else 0)
