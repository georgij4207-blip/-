# Монтажная раскладка силовой части в электроотсеке Salda RIS 400P (пластина 300 × 490 мм):
# устройства на своих местах (мм от левого верхнего угла пластины), клеммы, провода с номерами,
# сечением и цветом. Скрипт ПРОВЕРЯЕТ раскладку по схеме (ожидаемый список цепей ниже):
#   - каждый провод соединяет клеммы одной цепи;
#   - все клеммы каждой цепи соединены проводами (или заводскими/внешними соединениями);
#   - на винтовой клемме не больше двух проводов, в порту WAGO — один.
# Источники: docs/salda_in_unit.md (раздел 6, заводская схема), docs/fan_wiring.md (автотрансформатор),
# docs/panel_wiring.md («Вариант А2»), docs/boards_assembly.md (лист 5), профиль salda_ris400p.yaml.
# Положение заводских узлов — по фото (±10 мм); расположение клемм KC868-A8 — условно (по маркировке).
# Выход: docs/силовая_часть.pdf (A3 альбомный: обзор пластины, рейка крупно, таблица проводов).
#   python3 tools/build_compartment_wiring.py
import collections, os, pathlib, subprocess, sys
from html import escape

root = pathlib.Path(__file__).resolve().parent.parent

# ------------------------------------------------------------------ устройства
# id: (подпись, x, y, w, h, зона, {клемма: (dx, dy)})
DEV = {}
def dev(i, label, x, y, w, h, zone, terms):
    DEV[i] = (label, x, y, w, h, zone, terms)

def km(i, x):   # промежуточное реле: контакт сверху (230 В), катушка снизу (12 В)
    dev(i, i, x, 205, 6.2, 90, "230", {"11": (3.1, 6), "12": (3.1, 14), "14": (3.1, 22), "A1": (3.1, 76), "A2": (3.1, 84)})
def kl(i, x):   # контроль термостата: катушка 230 В сверху, сухой контакт снизу (реле повернуть на 180°)
    dev(i, i, x, 205, 6.2, 90, "230", {"A1": (3.1, 6), "A2": (3.1, 14), "11": (3.1, 68), "12": (3.1, 76), "14": (3.1, 84)})
def wago(i, x, y, n=5, h=90, zone="230"):
    dev(i, i, x, y, 9.5, h, zone, {str(k + 1): (4.75, 8 + k * (h - 16) / (n - 1)) for k in range(n)})

# заводское
dev("XIN", "ввод (заводской)", 45, 65, 60, 40, "fac", {"L": (10, 20), "N": (30, 20), "PE": (50, 20)})
dev("ATR", "автотрансформатор 460 ВА (заводской)", 165, 5, 130, 150, "fac",
    {"0": (15, 131), "80": (38, 131), "140": (61, 131), "170": (84, 131), "230": (107, 131)})
# рейка 122…288 мм
x = 122; dev("ES1", "упор", x, 205, 9, 90, "230", {}); x += 9
for k in range(1, 8): km(f"KM{k}", x); x += 6.2
for k in range(1, 4): kl(f"KL{k}", x); x += 6.2
dev("QF1", "QF1 3 А", x, 205, 18, 90, "230", {"1": (9, 6), "2": (9, 84)}); x += 18
dev("PSU", "БП HDR-30-12", x, 205, 35, 90, "230", {"L": (8, 6), "N": (17, 6), "+V": (19, 84), "-V": (28, 84)}); x += 35
wago("WL", x, 205); x += 10
wago("WN1", x, 205); x += 10
wago("WN2", x, 205); x += 10
dev("ES2", "упор", x, 205, 9, 90, "230", {})
RAIL_END = x + 9
# под рейкой
wago("W12", 116, 300, 5, 45, "12")
wago("W0", 127, 300, 5, 45, "12")
dev("SSR1", "SSR1 ТЭН", 150, 300, 58, 45, "230", {"1": (8, 6), "2": (50, 6), "3": (8, 39), "4": (50, 39)})
dev("SSR2", "SSR2 преднагр.", 222, 300, 58, 45, "230", {"1": (8, 6), "2": (50, 6), "3": (8, 39), "4": (50, 39)})
kc = {}
for k in range(1, 9):
    bx = 6 + (k - 1) * 24
    kc[f"K{k}COM"], kc[f"K{k}NO"], kc[f"K{k}NC"] = (bx, 6), (bx + 7, 6), (bx + 14, 6)
for k in range(1, 9):
    kc[f"X{k}"] = (10 + (k - 1) * 15, 104)
kc["GNDIN"], kc["12V"], kc["GND"] = (135, 104), (165, 104), (180, 104)
dev("KC", "KinCony KC868-A8 (клеммы — условно)", 90, 352, 200, 110, "12", kc)
dev("KEY", "модуль ключей", 12, 352, 72, 54, "12",
    {"J2.1": (4, 43), "J2.2": (4, 49), "J4.1": (68, 18), "J4.2": (68, 25), "J4.3": (68, 32), "J4.4": (68, 39)})
# в установке (через жгут и гермовводы)
EXT_X = 318
dev("HEAT", "ТЭН 1,5 кВт и термостаты (в установке)", EXT_X, 160, 110, 70, "ext",
    {"AT1": (8, 12), "H1": (8, 26), "RT1": (8, 40), "KE": (8, 54), "KE.N": (60, 54)})
dev("PREH", "преднагреватель 0,6 кВт и термостаты", EXT_X, 245, 110, 55, "ext",
    {"AT2": (8, 12), "RT2": (8, 26), "PE": (8, 40), "PE.N": (60, 40)})
dev("MOT", "моторы Ruck GE 160 (в установке)", EXT_X, 315, 110, 55, "ext",
    {"M2.L": (8, 12), "M2.N": (60, 12), "M1.L": (8, 30), "M1.N": (60, 30)})
dev("DMP", "привод заслонки 230 В", EXT_X, 385, 110, 40, "ext", {"L": (8, 14), "N": (40, 14), "PE": (72, 14)})

def T(ref):
    d, t = ref.split(":")
    label, x0, y0, w, h, zone, terms = DEV[d]
    dx, dy = terms[t]
    return x0 + dx, y0 + dy

# ------------------------------------------------------------------ ожидаемые цепи (по схеме)
NETS = {
    "L":    "XIN:L WL:1 WL:2 WL:3 WL:4 WL:5 QF1:1 ATR:230 HEAT:AT1 PREH:AT2",
    "L3А":  "QF1:2 PSU:L KM7:11",
    "N":    "XIN:N WN1:1 WN1:2 WN1:3 WN1:4 WN1:5 WN2:1 WN2:2 WN2:3 WN2:4 WN2:5 PSU:N ATR:0 HEAT:KE.N PREH:PE.N "
            "MOT:M1.N MOT:M2.N DMP:N KL1:A2 KL2:A2 KL3:A2",
    "PE":   "XIN:PE DMP:PE",
    "V1":   "ATR:80 KM1:14 KM4:14",
    "V2":   "ATR:140 KM2:14 KM5:14",
    "V3":   "ATR:230 KM3:14 KM6:14",
    "П-2":  "KM3:12 KM2:11",
    "П-1":  "KM2:12 KM1:11",
    "В-2":  "KM6:12 KM5:11",
    "В-1":  "KM5:12 KM4:11",
    "M2.L": "KM3:11 MOT:M2.L",
    "M1.L": "KM6:11 MOT:M1.L",
    "ЗАСЛ": "KM7:14 DMP:L",
    "H1":   "HEAT:H1 KL2:A1",
    "H2":   "HEAT:RT1 SSR1:1 KL1:A1",
    "ТЭН":  "SSR1:2 HEAT:KE",
    "P2":   "PREH:RT2 SSR2:1 KL3:A1",
    "ПРЕД": "SSR2:2 PREH:PE",
    "X1":   "KL1:14 KC:X1",
    "X5":   "KL2:14 KC:X5",
    "X6":   "KL3:14 KC:X6",
    "GIN":  "KL1:11 KL2:11 KL3:11 KC:GNDIN",
    "+12":  "PSU:+V W12:1 W12:2 W12:3 W12:4 W12:5 KC:12V KC:K3COM KC:K6COM KC:K7COM KEY:J2.1",
    "0В":   "PSU:-V W0:1 W0:2 W0:3 W0:4 KC:GND KEY:J2.2 " + " ".join(f"KM{k}:A2" for k in range(1, 8)),
    "K2C":  "KC:K3NC KC:K2COM", "K1C": "KC:K2NC KC:K1COM", "K5C": "KC:K6NC KC:K5COM", "K4C": "KC:K5NC KC:K4COM",
    **{f"KM{k}A1": f"KC:K{k}NO KM{k}:A1" for k in range(1, 8)},
    "SSR1+": "KEY:J4.1 SSR1:3", "SSR1-": "KEY:J4.2 SSR1:4", "SSR2+": "KEY:J4.3 SSR2:3", "SSR2-": "KEY:J4.4 SSR2:4",
}
NETS = {n: v.split() for n, v in NETS.items()}
# ATR:230 — и сетевой вход трансформатора, и отвод V3 (230 В) — один вывод: объединить цепи L и V3
NETS["L"] = list(dict.fromkeys(NETS["L"] + NETS.pop("V3")))

# ------------------------------------------------------------------ провода
# (цепь, откуда, куда, сечение мм², цвет, примечание)
W = []
def w(net, a, b, mm2, color, note=""): W.append((net, a, b, mm2, color, note))
BR, BU, BK, YG, RD, WH = "коричневый", "синий", "чёрный", "жёлто-зелёный", "красный", "белый"
# 230 В: ввод и распределение
w("L", "XIN:L", "WL:1", 1.5, BR, "ввод → WAGO L")
w("L", "WL:2", "QF1:1", 1.5, BR)
w("L", "WL:3", "ATR:230", 1.5, BR, "вход трансформатора (вместо платы)")
w("L", "WL:4", "HEAT:AT1", 1.5, BR, "к термостату AT1 (заводской провод Heating Stage 1)")
w("L", "WL:5", "PREH:AT2", 1.5, BR, "к термостату AT2 (заводской провод Heating Stage 2)")
w("L3А", "QF1:2", "PSU:L", 0.75, BR)
w("L3А", "QF1:2", "KM7:11", 0.75, BR, "заслонка — за автоматом 3 А")
w("N", "XIN:N", "WN1:1", 1.5, BU)
w("N", "WN1:2", "WN2:1", 1.5, BU, "перемычка между WAGO N")
w("N", "WN1:3", "PSU:N", 0.75, BU)
w("N", "WN1:4", "ATR:0", 1.5, BU, "вход трансформатора 0 В")
w("N", "WN1:5", "HEAT:KE.N", 1.5, BU, "N ТЭНа")
w("N", "WN2:2", "PREH:PE.N", 1.5, BU, "N преднагревателя")
w("N", "WN2:3", "MOT:M2.N", 0.75, BU, "к ЧЁРНОМУ проводу мотора притока (N)")
w("N", "WN2:4", "MOT:M1.N", 0.75, BU, "к ЧЁРНОМУ проводу мотора вытяжки (N)")
w("N", "WN2:5", "DMP:N", 0.75, BU)
w("N", "PSU:N", "KL1:A2", 0.75, BU, "N катушек KL")
w("N", "KL1:A2", "KL2:A2", 0.75, BU)
w("N", "KL2:A2", "KL3:A2", 0.75, BU)
w("PE", "XIN:PE", "DMP:PE", 0.75, YG)
# отводы трансформатора и цепочка ступеней
w("V1", "ATR:80", "KM1:14", 0.75, BK, "отвод ступени 1 (сверить)")
w("V1", "KM1:14", "KM4:14", 0.75, BK)
w("V2", "ATR:140", "KM2:14", 0.75, BK, "отвод ступени 2 (сверить)")
w("V2", "KM2:14", "KM5:14", 0.75, BK)
w("L", "ATR:230", "KM3:14", 0.75, BK, "отвод ступени 3 = 230 В")
w("L", "KM3:14", "KM6:14", 0.75, BK)
w("П-2", "KM3:12", "KM2:11", 0.75, BK, "цепочка NC притока")
w("П-1", "KM2:12", "KM1:11", 0.75, BK)
w("В-2", "KM6:12", "KM5:11", 0.75, BK, "цепочка NC вытяжки")
w("В-1", "KM5:12", "KM4:11", 0.75, BK)
w("M2.L", "KM3:11", "MOT:M2.L", 0.75, BK, "к СИНЕМУ проводу мотора притока (L)")
w("M1.L", "KM6:11", "MOT:M1.L", 0.75, BK, "к СИНЕМУ проводу мотора вытяжки (L)")
w("ЗАСЛ", "KM7:14", "DMP:L", 0.75, BK)
# нагрев
w("H1", "HEAT:H1", "KL2:A1", 0.75, BK, "от точки между AT1 и RT1")
w("H2", "HEAT:RT1", "SSR1:1", 1.5, BK, "выход RT1 (разорвать RT1 — ТЭН)")
w("H2", "SSR1:1", "KL1:A1", 0.75, BK)
w("ТЭН", "SSR1:2", "HEAT:KE", 1.5, BK, "вход ТЭНа")
w("P2", "PREH:RT2", "SSR2:1", 1.5, BK, "выход RT2 (разорвать RT2 — преднагреватель)")
w("P2", "SSR2:1", "KL3:A1", 0.75, BK)
w("ПРЕД", "SSR2:2", "PREH:PE", 1.5, BK, "вход преднагревателя")
# сухие контакты KL → входы платы (низкое напряжение)
w("X1", "KL1:14", "KC:X1", 0.5, WH)
w("X5", "KL2:14", "KC:X5", 0.5, WH)
w("X6", "KL3:14", "KC:X6", 0.5, WH)
w("GIN", "KL1:11", "KC:GNDIN", 0.5, BK, "общий входов платы")
w("GIN", "KL1:11", "KL2:11", 0.5, BK)
w("GIN", "KL2:11", "KL3:11", 0.5, BK)
# 12 В
w("+12", "PSU:+V", "W12:1", 0.75, RD)
w("+12", "W12:2", "KC:12V", 0.75, RD, "питание платы")
w("+12", "W12:3", "KC:K3COM", 0.5, RD, "катушки ступеней притока")
w("+12", "W12:4", "KC:K6COM", 0.5, RD, "катушки ступеней вытяжки")
w("+12", "W12:5", "KC:K7COM", 0.5, RD, "катушка заслонки")
w("+12", "KC:12V", "KEY:J2.1", 0.5, RD, "модуль ключей")
w("0В", "PSU:-V", "W0:1", 0.75, BK)
w("0В", "W0:2", "KC:GND", 0.75, BK)
w("0В", "W0:3", "KM1:A2", 0.5, BK, "общий катушек KM")
for k in range(1, 7):
    w("0В", f"KM{k}:A2", f"KM{k+1}:A2", 0.5, BK)
w("0В", "W0:4", "KEY:J2.2", 0.5, BK)
for a, b, n in (("K3NC", "K2COM", "K2C"), ("K2NC", "K1COM", "K1C"), ("K6NC", "K5COM", "K5C"), ("K5NC", "K4COM", "K4C")):
    w(n, f"KC:{a}", f"KC:{b}", 0.5, RD, "цепочка NC реле платы")
for k in range(1, 8):
    w(f"KM{k}A1", f"KC:K{k}NO", f"KM{k}:A1", 0.5, WH, f"катушка KM{k}")
for k, (a, b) in enumerate((("J4.1", "SSR1:3"), ("J4.2", "SSR1:4"), ("J4.3", "SSR2:3"), ("J4.4", "SSR2:4"))):
    net = ["SSR1+", "SSR1-", "SSR2+", "SSR2-"][k]
    w(net, f"KEY:{a}", b, 0.5, RD if "+" in net else BK, "управление SSR")

# ------------------------------------------------------------------ проверка
def check():
    errs = []
    t2n = {}
    for n, ts in NETS.items():
        for t in ts:
            if t in t2n:
                errs.append(f"клемма {t} в двух цепях: {t2n[t]}, {n}")
            t2n[t] = n
            T(t)  # клемма существует
    count = collections.Counter()
    parent = {}
    def f(x):
        parent.setdefault(x, x)
        while parent[x] != x:
            parent[x] = parent[parent[x]]; x = parent[x]
        return x
    for d, (label, x0, y0, w_, h, zone, terms) in DEV.items():   # порты одного WAGO соединены внутри
        if d.startswith("W"):
            ports = [f"{d}:{t}" for t in terms if f"{d}:{t}" in t2n]
            for pt in ports[1:]:
                parent[f(pt)] = f(ports[0])
    for i, (net, a, b, mm2, col, note) in enumerate(W, 1):
        for t in (a, b):
            T(t)
            if t2n.get(t) != net:
                errs.append(f"провод {i} ({net}): клемма {t} — цепь {t2n.get(t)}")
            count[t] += 1
        parent[f(a)] = f(b)
    for t, c in count.items():
        d = t.split(":")[0]
        lim = 1 if d.startswith("W") else 2
        if c > lim:
            errs.append(f"на клемме {t} {c} провода (можно {lim})")
    for n, ts in NETS.items():
        if len({f(t) for t in ts}) > 1:
            errs.append(f"цепь {n} не соединена")
    if errs:
        sys.exit("ОШИБКИ:\n  " + "\n  ".join(errs))
    return count
count = check()
print(f"раскладка проверена: устройств {len(DEV)}, цепей {len(NETS)}, проводов {len(W)}")

# ------------------------------------------------------------------ рисование
ZF = {"230": ("#f6e1d8", "#c7461f"), "12": ("#dce8f4", "#1f5f9e"), "fac": ("#e3e8eb", "#7b8a94"), "ext": ("#f3efe2", "#8a6d10")}
WC = {BR: "#8b4513", BU: "#1f5fbf", BK: "#222", YG: "#3d9a3d", RD: "#d0021b", WH: "#9b9b9b"}
tags = collections.defaultdict(list)
for i, (net, a, b, mm2, col, note) in enumerate(W, 1):
    tags[a].append(i); tags[b].append(i)

def svg(view, scale, lines=True, fs=3.0):
    vx, vy, vw, vh = view
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vx} {vy} {vw} {vh}" width="{vw*scale:.0f}mm" height="{vh*scale:.0f}mm" '
         f'font-family="DejaVu Sans, sans-serif">', f'<rect x="{vx}" y="{vy}" width="{vw}" height="{vh}" fill="#fff"/>',
         '<rect x="0" y="0" width="300" height="490" fill="#f7f8f8" stroke="#000" stroke-width="0.8"/>',
         f'<rect x="122" y="246" width="{RAIL_END-122:.1f}" height="7.5" fill="#bbb"/>',
         '<line x1="5" y1="349" x2="295" y2="349" stroke="#c00" stroke-width="0.5" stroke-dasharray="3,2"/>',
         f'<text x="6" y="347" font-size="{fs*0.9}" fill="#c00">выше — 230 В · ниже — 12 В и сигналы</text>',
         '<rect x="8" y="215" width="108" height="130" fill="#eee" stroke="#999" stroke-dasharray="2,1.5"/>',
         f'<text x="62" y="282" font-size="{fs}" text-anchor="middle" fill="#666">конденсаторы C1, C2</text>',
         f'<text x="62" y="{282+fs*1.3}" font-size="{fs*0.85}" text-anchor="middle" fill="#666">заводские, не трогать</text>']
    if lines:
        for i, (net, a, b, mm2, col, note) in enumerate(W, 1):
            (x1, y1), (x2, y2) = T(a), T(b)
            o.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{WC[col]}" '
                     f'stroke-width="{0.3 + mm2*0.35:.2f}" opacity="0.55"/>')
    for d, (label, x0, y0, w_, h, zone, terms) in DEV.items():
        fill, stroke = ZF[zone]
        o.append(f'<rect x="{x0}" y="{y0}" width="{w_}" height="{h}" fill="{fill}" stroke="{stroke}" stroke-width="0.4"/>')
        if w_ < 12:
            cx, cy = x0 + w_ / 2, y0 + h / 2
            if d.startswith("W"):
                cy = y0 + 8 + 2.5 * (h - 16) / 4
            o.append(f'<text x="{cx:.1f}" y="{cy:.1f}" font-size="{fs*0.85}" text-anchor="middle" '
                     f'transform="rotate(-90 {cx:.1f} {cy:.1f})" font-weight="bold">{escape(label)}</text>')
        else:
            o.append(f'<text x="{x0+2}" y="{y0+fs+1}" font-size="{fs}" font-weight="bold">{escape(label)}</text>')
        for t, (dx, dy) in terms.items():
            tx, ty = x0 + dx, y0 + dy
            o.append(f'<circle cx="{tx:.1f}" cy="{ty:.1f}" r="0.7" fill="{stroke}"/>')
            nums = tags.get(f"{d}:{t}") or []
            if w_ < 12:   # узкие реле и WAGO: имя клеммы над точкой, номера проводов — столбиком под ней
                o.append(f'<text x="{tx:.1f}" y="{ty-1.0:.1f}" font-size="{fs*0.62}" text-anchor="middle" fill="#333">{escape(t)}</text>')
                for k, n in enumerate(nums):
                    o.append(f'<text x="{tx:.1f}" y="{ty+1.0+fs*0.7*(k+1):.1f}" font-size="{fs*0.7}" text-anchor="middle" '
                             f'font-weight="bold" fill="#c0392b">{n}</text>')
            else:
                o.append(f'<text x="{tx+1.3:.1f}" y="{ty+fs*0.35:.1f}" font-size="{fs*0.7}" fill="#333">{escape(t)}</text>')
                if nums:
                    o.append(f'<text x="{tx-1.3:.1f}" y="{ty+fs*0.35:.1f}" font-size="{fs*0.72}" text-anchor="end" '
                             f'font-weight="bold" fill="#c0392b">{",".join(map(str, nums))}</text>')
    o.append("</svg>")
    return "\n".join(o)

over = svg((-10, -10, 450, 510), 0.47, True, 3.6)
rail = svg((110, 196, 192, 160), 1.55, False, 2.6)
low = svg((0, 296, 300, 172), 1.25, False, 2.8)

def tname(t):
    d, k = t.split(":")
    return f"{d}.{k}" if d not in ("HEAT", "PREH", "MOT", "DMP") else {
        "HEAT": "ТЭН/термостаты", "PREH": "преднагр./термостаты", "MOT": "мотор", "DMP": "заслонка"}[d] + f" {k}"
rows = "".join(f"<tr><td><b>{i}</b></td><td>{escape(net)}</td><td>{escape(tname(a))}</td><td>{escape(tname(b))}</td>"
               f"<td>{str(mm2).replace('.', ',')}</td><td><span class='sw' style='background:{WC[col]}'></span>{col}</td>"
               f"<td>{escape(note)}</td></tr>" for i, (net, a, b, mm2, col, note) in enumerate(W, 1))
html = f"""<!doctype html><meta charset="utf-8"><style>
@page {{ size: A3 landscape; margin: 10mm; }} body {{ font-family: 'DejaVu Sans', sans-serif; font-size: 9.5pt; color: #111; }}
h1 {{ font-size: 15pt; margin: 0 0 2mm; }} p {{ margin: 1mm 0; max-width: 260mm; }} .sheet {{ break-after: page; }}
.cols {{ display: flex; gap: 8mm; align-items: flex-start; }} .cols > svg {{ flex: none; }}
table {{ border-collapse: collapse; font-size: 8pt; }} td, th {{ border: 1px solid #aaa; padding: 1px 4px; text-align: left; vertical-align: top; }}
.sw {{ display: inline-block; width: 9px; height: 9px; margin-right: 4px; border: 1px solid #555; vertical-align: -1px; }}
ul {{ margin: 1mm 0; padding-left: 5mm; }}
</style>
<div class="sheet"><h1>Силовая часть в отсеке: обзор пластины 300 × 490 мм (масштаб 1 : 2,1)</h1>
<p>Устройства на своих местах (мм от левого верхнего угла пластины). Линии — провода (цвет = цвет провода, толщина = сечение);
красные числа у клемм — номера проводов из таблицы. Справа — то, что стоит в установке и подключается через жгут.
Положение заводских узлов — по фото (±10 мм), сверить по месту. Расположение клемм KC868-A8 и автотрансформатора — условно, по маркировке.</p>
<div class="cols">{over}<div>
<p><b>Главное</b></p><ul>
<li>Контакты KM (11, 12, 14) — сверху, 230 В; катушки (A1, A2) — снизу, 12 В от реле платы.</li>
<li>KL1–KL3 повернуть на 180°: катушка 230 В сверху, сухой контакт снизу — к входам платы.</li>
<li>На отвод трансформатора — только NO-контакты (14); мотор — на COM (11) старшего реле.
У моторов Ruck: <b>L — синий, N — чёрный</b>, C — коричневый (к конденсатору, заводское).</li>
<li>Отводы 80 / 140 / 230 В — предположение, сверить по заводской разводке.</li>
<li>SSR стоят ПОСЛЕ термостатов: RT1 → SSR1 → ТЭН, RT2 → SSR2 → преднагреватель.</li>
<li>RC-гасители (0,1 мкФ + 100 Ом) — на клеммах моторов и привода заслонки, L–N.</li>
<li>Диоды 1N4007 на катушки KM (катод к A1) — если в реле нет встроенных.</li>
<li>Защита линии — дифавтомат 16 А тип A в домовом щитке. QF1 3 А — БП 12 В и заслонка.</li>
</ul></div></div></div>
<div class="sheet"><h1>DIN-рейка (230 В) крупно, масштаб 1,55 : 1</h1>
<p>Слева направо: упор, KM1–KM7 (6,2 мм), KL1–KL3 (6,2 мм), QF1 (18 мм), БП HDR-30-12 (35 мм), WAGO 221-5 на держателях: L, N, N; упор.
Рейка {RAIL_END-122:.0f} мм (от 122 до {RAIL_END:.0f} мм). Номера клемм реле — сверить с маркировкой своих реле и колодок.</p>
{rail}</div>
<div class="sheet"><h1>Низ пластины: SSR, WAGO 12 В, плата, модуль ключей — масштаб 1,25 : 1</h1>
<p>SSR — клеммы 1–2 (230 В) сверху, 3–4 (управление) снизу. W12 и W0 — WAGO 221-5 для +12 В и 0 В. Клеммы платы показаны условно — по маркировке на плате.</p>
{low}</div>
<div class="sheet"><h1>Таблица проводов ({len(W)} шт.) — монтировать по порядку номеров</h1>
<table><tr><th>№</th><th>Цепь</th><th>Откуда</th><th>Куда</th><th>мм²</th><th>Цвет</th><th>Примечание</th></tr>{rows}</table></div>
"""
out_html = root / "docs" / "силовая_часть.html"
out_pdf = root / "docs" / "силовая_часть.pdf"
out_html.write_text(html, encoding="utf-8")
js = f"""const {{ chromium }} = require('playwright');
(async () => {{ const b = await chromium.launch(); const p = await b.newPage();
await p.goto('file://' + {str(out_html)!r}); await p.waitForTimeout(300);
await p.pdf({{ path: {str(out_pdf)!r}, format: 'A3', landscape: true, printBackground: true, preferCSSPageSize: true }});
await b.close(); }})().catch(e => {{ console.error(e); process.exit(1); }});"""
env = dict(os.environ, NODE_PATH=os.environ.get("NODE_PATH", "/opt/node-tools/node_modules"))
subprocess.run(["node", "-e", js], check=True, env=env)
print(out_pdf)
