# Наглядная схема щитка одним рисунком: docs/схема_щитка_рисунок.svg (+ .pdf, A3 альбомный).
# Каждая клемма XT, разъём модуля и реле на рисунке сверяются с таблицами docs/panel_wiring.md;
# расхождения печатаются в конце и попадают на рисунок как «уточнить».
# Нужны Node.js с Playwright/Chromium (для PDF).
#   python3 tools/build_wiring_svg.py
import os, pathlib, re, shutil, subprocess, sys, tempfile
from xml.sax.saxutils import escape

root = pathlib.Path(__file__).resolve().parent.parent
out_svg = root / "docs" / "схема_щитка_рисунок.svg"
out_pdf = out_svg.with_suffix(".pdf")
doc = (root / "docs/panel_wiring.md").read_text(encoding="utf-8")
board = (root / "firmware/boards/kc868_a8.yaml").read_text(encoding="utf-8")
profile = (root / "firmware/profiles/salda_ris400p.yaml").read_text(encoding="utf-8")

# ---------------------------------------------------------------- сверка с таблицами
xt = {}  # номер клеммы → текст строки таблицы XT
for line in doc[doc.index("## 2. Клеммник"):doc.index("## 3. Силовая")].splitlines():
    c = [x.strip() for x in line.strip().strip("|").split("|")]
    if len(c) == 4 and re.match(r"^[\d,–\s]+$", c[1] or "x"):
        nums = []
        for part in re.split(r",\s*", c[1]):
            if "–" in part:
                a, b = part.split("–"); nums += range(int(a), int(b) + 1)
            elif part.strip():
                nums.append(int(part))
        for n in nums:
            xt[n] = c[2]
relays = {}
for line in doc[doc.index("### Реле платы K1–K8"):].splitlines():
    m = re.match(r"\| (K\d) \| (.*?) \| (.*?) \| (.*?) \|", line)
    if m:
        relays[m.group(1)] = m.groups()[1:]
jtab = doc[doc.index("### Модуль ключей SSR"):doc.index("### Дискретные входы")]

problems = []


def check(cond, what):
    if not cond:
        problems.append(what)
    return cond


def xtok(n, *words):
    """Клемма n есть в таблице XT и её строка содержит все слова."""
    row = xt.get(n, "")
    return check(row and all(w.lower() in row.lower() for w in words),
                 f"XT {n}: на рисунке «{' / '.join(words)}», в таблице «{row or 'нет такой клеммы'}»")


def relok(k, col, *words):
    row = relays.get(k, ("", "", ""))[col]
    return check(all(w in row for w in words), f"{k} {['COM', 'NO', 'NC'][col]}: на рисунке «{' '.join(words)}», в таблице «{row}»")


def jok(*words):
    return check(all(w in jtab for w in words), f"модуль ключей: «{' '.join(words)}» нет в таблице разъёмов")


def fwok(text, src, what):
    return check(text in src, f"прошивка: {what}")


# ---------------------------------------------------------------- рисование
RED, BLUE, GREY, INK = "#c62828", "#1565c0", "#777777", "#1a1a1a"
FS = 2.9          # 8,2 pt: мельче не бывает
FSS = 3.2
el = []           # линии и рамки по порядку
top = []          # клеммы, контакты, метки и текст — поверх линий


def t(x, y, s, size=FS, anchor="start", color=INK, weight="normal", rot=0, italic=False):
    tr = f' transform="rotate({rot} {x:.2f} {y:.2f})"' if rot else ""
    st = ' font-style="italic"' if italic else ""
    top.append(f'<text x="{x:.2f}" y="{y:.2f}" font-size="{size}" text-anchor="{anchor}" fill="{color}" '
               f'font-weight="{weight}"{st}{tr}>{escape(s)}</text>')


def ln(pts, color=BLUE, dash=False, arrow=False, w=0.45):
    d = "M" + " L".join(f"{x:.2f},{y:.2f}" for x, y in pts)
    el.append(f'<path d="{d}" fill="none" stroke="#fff" stroke-width="{w + 1.3}" stroke-linejoin="round"/>')
    da = ' stroke-dasharray="1.6,1.1"' if dash else ""
    mk = f' marker-end="url(#a{"r" if color == RED else "b"})"' if arrow else ""
    el.append(f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{w}" stroke-linejoin="round"{da}{mk}/>')


def box(x, y, w, h, lines=(), dash=False, fill="#ffffff", stroke=INK, size=FS, rot=False, bold_first=False, sw=0.35):
    da = ' stroke-dasharray="1.6,1.1"' if dash else ""
    el.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" rx="0.8" fill="{fill}" '
              f'stroke="{stroke}" stroke-width="{sw}"{da}/>')
    n = len(lines)
    for i, s in enumerate(lines):
        wt = "bold" if bold_first and i == 0 else "normal"
        if rot:
            cx = x + w / 2 + (i - (n - 1) / 2) * size * 1.15 + size * 0.35
            t(cx, y + h / 2, s, size, "middle", weight=wt, rot=-90)
        else:
            cy = y + h / 2 + (i - (n - 1) / 2) * size * 1.18 + size * 0.35
            t(x + w / 2, cy, s, size, "middle", weight=wt)


def term(x, y, label=None, pos="right", color=INK, size=FS):
    top.append(f'<rect x="{x - 0.9:.2f}" y="{y - 0.9:.2f}" width="1.8" height="1.8" fill="#fff" stroke="{color}" stroke-width="0.35"/>')
    if label:
        dx = {"right": (2, 0.9, "start"), "left": (-2, 0.9, "end"), "above": (0, -1.6, "middle"),
              "below": (0, 3.6, "middle")}[pos]
        t(x + dx[0], y + dx[1], label, size, dx[2])


def pin(x, y):
    top.append(f'<rect x="{x - 0.8:.2f}" y="{y - 0.8:.2f}" width="1.6" height="1.6" fill="{INK}"/>')


def dot(x, y, color=BLUE):
    top.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="0.65" fill="{color}"/>')


def tag(x, y, s, color=BLUE):
    top.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="2.3" fill="#fff" stroke="{color}" stroke-width="0.4"/>')
    t(x, y + 1.05, s, FS, "middle", color, "bold")


def stub(x, y, s, color=RED, anchor="start"):
    """Конец линии с надписью «куда» (шина, автомат)."""
    t(x, y + 1.0, s, FS, anchor, color, "bold")


# ================================================================ заголовок и зоны
check(len(xt) > 40 and len(relays) == 8, "таблицы panel_wiring.md не прочитаны (XT / K1–K8)")
t(5, 9, "Схема подключения щитка", 5.0, weight="bold")
t(5, 14.5, "Salda RIS 400P / KinCony KC868-A8", 3.6, color="#333")
SEP = 295
el.append(f'<line x1="{SEP}" y1="20" x2="{SEP}" y2="292" stroke="#999" stroke-width="0.3" stroke-dasharray="2,1.5"/>')
SX = SEP + 4
t(SX, 24, "СИЛОВАЯ ЧАСТЬ 230 В", 4.0, weight="bold", color=RED)
t(3, 278, "Номера клемм XT — docs/panel_wiring.md;", FS, color="#444")
t(3, 282, "входы, каналы — profiles/salda_ris400p.yaml;", FS, color="#444")
t(3, 286, "номиналы — docs/bom.md.", FS, color="#444")
t(3, 290, "Расположение контактов — условное.", FS, color="#444")

# ================================================================ плата
BX, BY, BW, BH = 105, 74, 123, 150
RX, BB = BX + BW, BY + BH
box(BX, BY, BW, BH, fill="#f4f6f8", sw=0.6)
cx = BX + 52
t(cx, BY + 62, "KinCony KC868-A8", 4.4, "middle", weight="bold")
t(cx, BY + 67.5, "ESP32", FSS, "middle", color="#555")
t(cx, BY + 72.5, "реле K1–K8: PCF8574 0x24", FS, "middle", color="#555")
t(cx, BY + 76.5, "входы X1–X8: PCF8574 0x22", FS, "middle", color="#555")
t(cx, BY + 82, "названия контактов — по описанию", FS, "middle", color="#a33", italic=True)
t(cx, BY + 85.8, "платы, сверить с шелкографией", FS, "middle", color="#a33", italic=True)
fwok("pcf_relays_address: \"0x24\"", board, "адрес PCF8574 реле 0x24")
fwok("pcf_inputs_address: \"0x22\"", board, "адрес PCF8574 входов 0x22")

# --- верх: GND входов, X1–X8
XGND = BX + 6
XP = {f"X{i + 1}": BX + 16 + i * 14 for i in range(8)}
pin(XGND, BY); t(XGND, BY + 4.6, "GND", FS, "middle", weight="bold"); t(XGND + 0.5, BY + 8.3, "входов", FS, "middle")
for name, x in XP.items():
    pin(x, BY); t(x, BY + 4.6, name, FS, "middle", weight="bold")

# --- лево: P19, TEM/HUMI 3, 4
P19 = {"3V": BY + 18, "GND": BY + 24, "SDA": BY + 30, "SCL": BY + 36}
t(BX + 2.2, BY + 13.5, "P19", FSS, weight="bold")
for k, y in P19.items():
    pin(BX, y); t(BX + 2.2, y + 1, k, FS)
TH3D, TH3G, TH4D = BY + 95, BY + 101, BY + 113
for y, s in ((TH3D, "TEM/HUMI 3: DATA (GPIO36)"), (TH3G, "TEM/HUMI 3: GND"), (TH4D, "TEM/HUMI 4: DATA (GPIO39), резерв")):
    pin(BX, y); t(BX + 2.2, y + 1, s, FS)
t(BX + 2.2, TH4D + 5, "DATA/GND портов — уточнить", FS, color="#a33", italic=True)
fwok("t_supply_pin: GPIO36", profile, "t_supply на GPIO36")

# --- низ: TEM/HUMI 1, 2, питание
TH1, TH2, P12, PG = BX + 28, BX + 50, BX + 80, BX + 96
for x, a, b in ((TH1, "TEM/HUMI 1", "DATA GPIO14"), (TH2, "TEM/HUMI 2", "DATA GPIO13"), (P12, "12V", "питание"), (PG, "GND", "питание")):
    pin(x, BB); t(x, BB - 6.3, a, FS, "middle", weight="bold"); t(x, BB - 2.6, b, FS, "middle")
fwok("ssr_pin: GPIO14", profile, "ТЭН на GPIO14"); fwok("preheater_pin: GPIO13", profile, "преднагрев на GPIO13")

# --- право: реле K1–K8 (на плате)
RY = {f"K{i + 1}": BY + 14 + i * 16 for i in range(8)}
for k, y in RY.items():
    box(RX - 21, y - 2.2, 11, 14.4, [k], fill="#fff", size=FSS, bold_first=True)
    for j, s in enumerate(("COM", "NO", "NC")):
        pin(RX, y + j * 5); t(RX - 1.6, y + j * 5 + 1, s, FS, "end")
COM = {k: (RX, y) for k, y in RY.items()}
NO = {k: (RX, y + 5) for k, y in RY.items()}
NC = {k: (RX, y + 10) for k, y in RY.items()}

# ================================================================ лево: датчики, XT7, ADS1115
AX, AY, AW, AH = 60, 76, 30, 78
box(AX, AY, AW, AH, fill="#fff")
t(AX + AW / 2, AY + 5, "ADS1115 (0x48)", FSS, "middle", weight="bold")
t(AX + AW / 2, AY + 9, "+ плата делителей", FS, "middle")
ADS = {"A2": AY + 22, "A1": AY + 37, "A0": AY + 52, "A3": AY + 67}
for k, y in ADS.items():
    pin(AX, y); t(AX + 2, y + 1, k, FSS, weight="bold")
t(AX + 2, ADS["A3"] + 4.6, "опорное от 3V", FS, color="#555")
ADSR = {"VDD": P19["3V"], "GND": P19["GND"], "SDA": P19["SDA"], "SCL": P19["SCL"]}
for k, y in ADSR.items():
    pin(AX + AW, y); t(AX + AW - 2, y + 1, k, FS, "end")
fwok("ads1115_address: \"0x48\"", profile, "ADS1115 0x48")

sensors = [
    ("t_outdoor", "наружный воздух", (28, 29), "A2", "t_outdoor_channel: A2_GND"),
    ("t_exhaust", "выброс", (30, 31), "A1", "t_exhaust_channel: A1_GND"),
    ("t_extract", "вытяжка из помещения", (32, 33), "A0", "t_extract_channel: A0_GND"),
    ("t_supply", "приток", (34, 35), "TH3", "t_supply_pin: GPIO36"),
]
XT7X = 46
t(XT7X, AY + 9, "XT7", FSS, "middle", weight="bold")
for name, where, (a, b), ch, prof in sensors:
    ys, yg = (ADS[ch], ADS[ch] + 5) if ch != "TH3" else (TH3D, TH3G)
    box(3, ys - 4, 35, 12, [f"NTC {name}", where], bold_first=True)
    ln([(38, ys), (XT7X, ys)]); ln([(38, yg), (XT7X, yg)])
    term(XT7X, ys, str(a), "above"); term(XT7X, yg, str(b), "below")
    xtok(a, name); xtok(b, name)
    fwok(prof, profile, f"{name}: {prof}")
    if ch != "TH3":
        ln([(XT7X, ys), (AX, ys)], arrow=True)
        t(AX - 2, ys - 1.3, f"→ {ch}", FS, "end", BLUE, "bold")
        ln([(XT7X, yg), (52, yg)]); t(53, yg + 1, "GND ADS", FS, color=BLUE)
    else:
        ln([(XT7X, ys), (BX, ys)], arrow=True); ln([(XT7X, yg), (BX, yg)], arrow=True)
        t(52, ys - 1.3, "→ TEM/HUMI 3 (GPIO36)", FS, color=BLUE, weight="bold")
        t(52, yg + 4, "→ GND порта 3", FS, color=BLUE)
t(3, 222, "Экран кабелей датчиков —", FS, color="#555")
t(3, 226, "на GND с одной стороны (в щите)", FS, color="#555")

# P19 → ADS1115 и OLED/SCD41
for k, ka in (("3V", "VDD"), ("GND", "GND"), ("SDA", "SDA"), ("SCL", "SCL")):
    ln([(AX + AW, ADSR[ka]), (BX, P19[k])])
OLY = 196
box(AX, OLY, AW, 13, ["OLED SSD1306,", "CO2 SCD41"])
ln([(AX + AW, OLY + 6.5), (94, OLY + 6.5), (94, P19["SCL"])]); dot(94, P19["SCL"])
t(AX - 2, OLY + 17, "к тем же P19:", FS, color=BLUE)
t(AX - 2, OLY + 21, "3V, GND, SDA, SCL", FS, color=BLUE)

# ================================================================ верх: входы X1–X8, XT8
SRCY, SRCH = 15, 30
XTY = 56          # ряд клемм XT8
GBY = 67          # шина «GND входов»
ln([(XGND, BY - 0.8), (XGND, GBY), (BX + 112, GBY)])
t(XGND + 2, GBY - 1.3, "GND", FS, color=BLUE)


def src(cx, lines, dash=False, w=12):
    box(cx - w / 2, SRCY, w, SRCH, lines, dash=dash, rot=True)


# X1, X5, X6 — контакты KL1, KL2, KL3 (в щите, без XT)
for xn, kl, l2, dash in (("X1", "KL1 (11–14)", "терм. +100 °C", False), ("X5", "KL2 (11–14)", "терм. +50 °C", False),
                         ("X6", "KL3 (11–14)", "по желанию", True)):
    x = XP[xn]
    src(x + 2.5, [kl, l2], dash)
    ln([(x, SRCY + SRCH), (x, BY - 0.8)], dash=dash, arrow=True)
    ln([(x + 5, SRCY + SRCH), (x + 5, GBY)], dash=dash); dot(x + 5, GBY)
check("KL1 NO (11–14)" in doc and "KL2 NO (11–14)" in doc, "KL1/KL2: контакты 11–14 не совпадают с таблицей входов")
check("th50_enabled: \"true\"" in profile, "профиль: th50_enabled не \"true\" — KL2 на X5 не используется")
check("preheat_th_enabled: \"false\"" in profile, "профиль: preheat_th_enabled изменился — пересмотреть KL3/X6")

# X2, X3, X4, X8 — через XT8, перемычка — пунктирная дуга
xt8 = [("X2", 36, 37, ["фильтр притока", "реле (NC)"], ("X2", "фильтр")),
       ("X3", 38, 39, ["фильтр вытяжки", "реле (NC)"], ("X3", "фильтр")),
       ("X4", 40, 41, ["внешний стоп", "(NC)"], ("X4", "стоп")),
       ("X8", 42, 43, ["пожарная сигн.", "(NC)"], ("X8", "пожар"))]


def jumper(xa, xb, y):
    el.append(f'<path d="M{xa:.2f},{y:.2f} Q{(xa + xb) / 2:.2f},{y - 3.6:.2f} {xb:.2f},{y:.2f}" '
              f'fill="none" stroke="{BLUE}" stroke-width="0.45" stroke-dasharray="1.2,0.9"/>')


for xn, a, b, lines, words in xt8:
    xa = XP[xn]; xb = xa + 5
    src(xa + 2.5, lines)
    for x, n in ((xa, a), (xb, b)):
        ln([(x, SRCY + SRCH), (x, XTY)]); term(x, XTY, str(n), "above")
    jumper(xa, xb, XTY - 5.2)
    ln([(xa, XTY), (xa, BY - 0.8)], arrow=True)
    ln([(xb, XTY), (xb, GBY)]); dot(xb, GBY)
    xtok(a, *words); xtok(b, "GND")

# X7 — от J5.1 модуля ключей
src(XP["X7"], ["от J5.1 модуля", "ключей (U3)"])
ln([(XP["X7"], SRCY + SRCH), (XP["X7"], BY - 0.8)], arrow=True)
tag(XP["X7"] + 4.5, SRCY + SRCH + 4.5, "A")
jok("J5 (вход X7)", "1 коллектор U3", "плата X7"); jok("2 эмиттер U3", "плата GND входов")

# XT8.44–45 — реле давления (цепь +12V_FLOW, не вход платы)
DR = (XP["X8"] + 14, XP["X8"] + 19)
src(DR[0] + 2.5, ["реле давл. ДР", "COM / NO"])
for x, n, tg in ((DR[0], 44, "B"), (DR[1], 45, "C")):
    ln([(x, SRCY + SRCH), (x, XTY)]); term(x, XTY, str(n), "above")
    ln([(x, XTY), (x, XTY + 6.5)]); tag(x, XTY + 9, tg)
xtok(44, "ДР", "COM", "+12"); xtok(45, "ДР", "NO", "+12V_FLOW")
jok("XT8.44 → ДР COM"); jok("XT8.45 ← ДР NO")

# XT8.46–47 — K8 наружу
K8X = (DR[0] + 14, DR[0] + 19)
src(K8X[0] + 2.5, ["K8 «норма»", "наружу ≤ 24 В"], dash=True)
for x, n in ((K8X[0], 46), (K8X[1], 47)):
    ln([(x, SRCY + SRCH), (x, XTY)], dash=True); term(x, XTY, str(n), "above")
xtok(46, "K8", "COM"); xtok(47, "K8", "NO")
relok("K8", 0, "XT8.46"); relok("K8", 1, "XT8.47")
t(XGND - 2, XTY + 1, "XT8", FSS, "end", weight="bold")
el.append(f'<rect x="{XP["X2"] - 3:.2f}" y="{XTY - 1.6:.2f}" width="{K8X[1] - XP["X2"] + 6:.2f}" height="3.2" '
          f'fill="none" stroke="#999" stroke-width="0.25" stroke-dasharray="1,1"/>')

# ================================================================ право: реле, цепочки, XT2/XT3/XT6
LA, LB = RX + 4, RX + 8          # коридоры цепочек NC → COM
XTX = 262                        # клеммы XT2, XT3, XT6
MBX = 279                        # нагрузки


def xt_term(y, label):
    term(XTX, y, None, color=RED)
    t(XTX + 1.6, y - 1.5, label, FS, color=RED)


def feed(k, qf):
    y = COM[k][1]
    ln([(RX + 17, y), (RX + 0.9, y)], RED, arrow=True)
    t(RX + 12, y - 1.3, qf, FS, "middle", RED, "bold")


def load(y0, y1, lines):
    box(MBX, y0, 14, y1 - y0, lines, stroke=RED)


def chain(k_hi, k_mid, k_lo, qf, xt_name, nums, lines):
    ln([NC[k_hi], (LA, NC[k_hi][1]), (LA, COM[k_mid][1]), (RX + 0.9, COM[k_mid][1])], RED, arrow=True)
    ln([NC[k_mid], (LB, NC[k_mid][1]), (LB, COM[k_lo][1]), (RX + 0.9, COM[k_lo][1])], RED, arrow=True)
    relok(k_mid, 0, f"{k_hi}.NC"); relok(k_lo, 0, f"{k_mid}.NC"); relok(k_hi, 2, f"{k_mid}.COM"); relok(k_mid, 2, f"{k_lo}.COM")
    relok(k_hi, 0, qf)
    feed(k_hi, qf)
    ys = []
    for k, n in zip((k_lo, k_mid, k_hi), nums[:3]):
        y = NO[k][1]; ys.append(y)
        ln([NO[k], (XTX - 0.9, y)], RED, arrow=True)
        xt_term(y, f"{xt_name}.{n}")
        relok(k, 1, f"{xt_name}.{n}")
    yN, yPE = ys[-1] + 5.5, ys[-1] + 11
    xt_term(yN, f"{xt_name}.{nums[3]}"); xt_term(yPE, f"{xt_name}.{nums[4]}")
    stub(XTX - 2, yN, "N", RED, "end"); stub(XTX - 2, yPE, "PE", RED, "end")
    y0, y1 = ys[0] + 3, ys[2] - 3
    load(y0, y1, lines)
    for y in ys:
        yy = min(max(y, y0 + 2), y1 - 2)
        ln([(XTX, y), (MBX - 3, y), (MBX - 3, yy), (MBX - 0.8, yy)] if y != yy else [(XTX, y), (MBX - 0.8, y)], RED, arrow=True)
    ln([(XTX, yN), (MBX + 7, yN), (MBX + 7, y1 + 0.8)], RED, arrow=True)
    t(XTX + 10, yPE + 1, "→ корпус", FS, color=RED)


chain("K3", "K2", "K1", "QF4", "XT2", (4, 5, 6, 7, 8), ["мотор", "притока"])
chain("K6", "K5", "K4", "QF5", "XT3", (9, 10, 11, 12, 13), ["мотор", "вытяжки"])
for n, w in ((4, "скорость 1"), (5, "скорость 2"), (6, "скорость 3"), (7, "N"), (8, "PE"),
             (10, "2"), (12, "N"), (13, "PE"), (25, "K7"), (26, "N"), (27, "PE")):
    xtok(n, w)
xtok(9, "1/2/3"); xtok(11, "1/2/3")

# K7 → XT6 → привод заслонки
feed("K7", "QF6")
relok("K7", 0, "QF6"); relok("K7", 1, "XT6.25")
y7 = NO["K7"][1]
ln([NO["K7"], (XTX - 0.9, y7)], RED, arrow=True)
xt_term(y7, "XT6.25"); xt_term(y7 + 5.5, "XT6.26"); xt_term(y7 + 11, "XT6.27")
stub(XTX - 2, y7 + 5.5, "N", RED, "end"); stub(XTX - 2, y7 + 11, "PE", RED, "end")
load(y7 - 3, y7 + 9, ["привод", "заслонки"])
ln([(XTX, y7), (MBX - 0.8, y7)], RED, arrow=True); ln([(XTX, y7 + 5.5), (MBX - 0.8, y7 + 5.5)], RED)
t(XTX + 10, y7 + 12, "→ корпус", FS, color=RED)

# K8 → XT8.46/47 (сухой контакт)
for j, x in ((0, K8X[0]), (1, K8X[1])):
    y = RY["K8"] + j * 5
    ln([(RX, y), (x, y), (x, XTY + 0.9)], BLUE, dash=True, arrow=True)
t(K8X[1] + 1.5, RY["K8"] + 3.6, "K8 → XT8.46/47", FS, color=BLUE)

# ================================================================ низ: модуль ключей, БП
MX, MY, MW, MH = 105, 240, 95, 50
box(MX, MY, MW, MH, fill="#f7f9fc", sw=0.6)
t(MX + 40, MY + 21, "Модуль ключей SSR", 4.0, "middle", weight="bold")
t(MX + 40, MY + 26, "(самодельный, ssr_driver.md)", FS, "middle")
t(MX + 40, MY + 31.5, "CH1 → U1/Q1 → SSR1 (ТЭН)", FS, "middle")
t(MX + 40, MY + 35.5, "CH2 → U2/Q2 → SSR2 (преднагрев)", FS, "middle")
t(MX + 40, MY + 39.5, "+12V_FLOW → U3 → X7", FS, "middle")
J1 = [("J1.1", "+3V3", MX + 8), ("J1.2", "GND", MX + 18), ("J1.3", "CH1", TH1), ("J1.4", "CH2", TH2)]
t(MX + 2, MY + 13, "J1", FSS, weight="bold")
for jn, s, x in J1:
    pin(x, MY); t(x, MY + 4.4, s, FS, "middle"); t(x, MY + 8, jn, FS, "middle", color="#555")
ln([(TH1, BB), (TH1, MY - 0.8)], arrow=True); ln([(TH2, BB), (TH2, MY - 0.8)], arrow=True)
t(TH1 + 1.5, BB + 7.5, "GPIO14", FS, color=BLUE, weight="bold"); t(TH2 + 1.5, BB + 7.5, "GPIO13", FS, color=BLUE, weight="bold")
jok("3 CH1", "GPIO14"); jok("4 CH2", "GPIO13")
ln([(BX, P19["3V"]), (99, P19["3V"]), (99, 231), (MX + 8, 231), (MX + 8, MY - 0.8)], arrow=True); dot(99, P19["3V"])
ln([(BX, P19["GND"]), (101.5, P19["GND"]), (101.5, 234.5), (MX + 18, 234.5), (MX + 18, MY - 0.8)], arrow=True); dot(101.5, P19["GND"])
t(97, 229.5, "P19 3V, GND → J1", FS, "end", BLUE)
jok("1 +3V3", "P19 «3V»"); jok("2 GND", "P19 «GND»")

# J5 (слева) → X7 и GND входов
for y, s, tg in ((MY + 20, "J5.1 → X7", "A"), (MY + 27, "J5.2 → GND входов", "D")):
    pin(MX, y); t(MX + 2, y + 1, s.split()[0], FS)
    ln([(MX, y), (MX - 8, y)]); tag(MX - 10.4, y, tg); t(MX - 13.5, y + 1, s, FS, "end", BLUE)
tag(XGND - 4, GBY, "D")
ln([(XGND - 1.7, GBY), (XGND, GBY)])

# J2, J3, J4 справа
RJ = MX + MW
JR = [("J2.1", "+12 В", MY + 5), ("J2.2", "GND", MY + 10), ("J3.1", "+12 В (F1)", MY + 17), ("J3.2", "+12V_FLOW", MY + 22),
      ("J4.1", "SSR1 +", MY + 29), ("J4.2", "SSR1 −", MY + 34), ("J4.3", "SSR2 +", MY + 40), ("J4.4", "SSR2 −", MY + 45)]
JY = {}
for jn, s, y in JR:
    JY[jn] = y; pin(RJ, y); t(RJ - 1.8, y + 1, f"{jn} {s}", FS, "end")
for jn, tg, s in (("J3.1", "B", "→ XT8.44 → ДР COM"), ("J3.2", "C", "← XT8.45 ← ДР NO")):
    y = JY[jn]; ln([(RJ, y), (RJ + 6, y)]); tag(RJ + 8.4, y, tg); t(RJ + 12, y + 1, s, FS, color=BLUE)

# БП 12 В: справа от J2, +V/−V прямо на J2 и вверх на плату
PX, PY = 216, JY["J2.1"] - 4
box(PX, PY, 26, 13, ["БП 12 В"], bold_first=True, stroke=RED, size=FSS)
for dy, s_ in ((3.5, "L от QF6"), (9, "N")):
    ln([(PX + 33, PY + dy), (PX + 26.8, PY + dy)], RED, arrow=True); t(PX + 34, PY + dy + 1, s_, FS, color=RED, weight="bold")
t(PX + 1.5, JY["J2.1"] + 1, "+V", FS, color=BLUE, weight="bold"); t(PX + 1.5, JY["J2.2"] + 1, "−V", FS, color=BLUE, weight="bold")
ln([(PX, JY["J2.1"]), (RJ + 0.9, JY["J2.1"])], arrow=True)
ln([(PX, JY["J2.2"]), (RJ + 0.9, JY["J2.2"])], arrow=True)
ln([(205, JY["J2.1"]), (205, BB + 4), (P12, BB + 4), (P12, BB + 0.8)], arrow=True); dot(205, JY["J2.1"])
ln([(209, JY["J2.2"]), (209, BB + 7), (PG, BB + 7), (PG, BB + 0.8)], arrow=True); dot(209, JY["J2.2"])
jok("J2 (питание)", "БП +V"); jok("БП −V")

# ================================================================ силовая часть
def qf(x, y, name):
    box(x, y - 3, 13, 6, [name], stroke=RED, bold_first=True)


t(SX, 33, "Ввод", FSS, weight="bold")
for i, (n, s) in enumerate(((1, "L"), (2, "N"), (3, "PE"))):
    y = 39 + i * 6
    term(SX + 1, y, None, color=RED); t(SX + 3, y + 1, f"XT1.{n} {s}", FS, color=RED)
    xtok(n, s)
    ln([(SX + 16, y), (SX + 31.2 if s != "PE" else SX + 66, y)], RED, arrow=True)
box(SX + 32, 36, 21, 12, ["QF1", "дифавтомат"], stroke=RED, bold_first=True)
for i, s in enumerate(("шина L", "шина N")):
    ln([(SX + 53, 39 + i * 6), (SX + 66, 39 + i * 6)], RED, arrow=True); stub(SX + 67, 39 + i * 6, s)
stub(SX + 67, 51, "шина PE")

t(SX, 64, "Автоматы от шины L", FSS, weight="bold")
feeds = [("QF2", "→ XT4.14, ТЭН (ниже)"), ("QF3", "→ XT5.20, преднагреватель (ниже)"),
         ("QF4", "→ K3.COM, мотор притока"), ("QF5", "→ K6.COM, мотор вытяжки"),
         ("QF6", "→ БП 12 В и K7.COM (заслонка)")]
for i, (q, s) in enumerate(feeds):
    y = 71 + i * 8
    stub(SX, y, "L", RED); ln([(SX + 3.5, y), (SX + 7.2, y)], RED)
    qf(SX + 8, y, q); ln([(SX + 21, y), (SX + 26, y)], RED, arrow=True)
    t(SX + 27, y + 1, s, FS, color=RED)
xtok(14, "термостата +100"); xtok(20, "преднагревателя")

LY = 118
t(SX, LY, "Обозначения", FSS, weight="bold")
lg = [("line", RED, False, "230 В; цепочки K3→K2→K1, K6→K5→K4: NC → COM"), ("line", BLUE, False, "12 В, 3,3 В, сигналы, I2C"),
      ("line", BLUE, True, "по желанию / если есть"), ("arc", None, None, "перемычка, если сигнала нет"),
      ("term", None, None, "клемма щита XT (номер)"), ("pin", None, None, "контакт платы или модуля"),
      ("dbox", None, None, "в установке, не в щите"), ("tag", None, None, "одна буква — одна линия")]
for i, (kind, c, dash, s) in enumerate(lg):
    y = LY + 6 + i * 5.6
    if kind == "line":
        ln([(SX, y), (SX + 12, y)], c, dash=dash, arrow=True)
    elif kind == "term":
        term(SX + 6, y)
    elif kind == "pin":
        pin(SX + 6, y)
    elif kind == "dbox":
        box(SX + 1, y - 2.2, 10, 4.4, dash=True, stroke=GREY)
    elif kind == "tag":
        tag(SX + 6, y, "A")
    elif kind == "arc":
        jumper(SX + 2, SX + 10, y + 1.5)
    t(SX + 15, y + 1, s, FS)
t(SX, LY + 53, "SSR: 1–2 — нагрузка, 3 (+) / 4 (−) — управление;", FS, color="#a33", italic=True)
t(SX, LY + 57, "клеммы типового SSR-25DA — уточнить по маркировке", FS, color="#a33", italic=True)


def thermo(x0, y, w, lines):
    box(x0, y - 4, w, 8, lines, dash=True, stroke=GREY)


def ssr_box(x, ytop, ybot, name):
    box(x - 7, ytop - 3.5, 14, ybot - ytop + 7, [name], stroke=RED, bold_first=True, size=FSS)
    t(x + 8, ytop - 1.4, "1", FS, color=RED); t(x + 8, ybot + 3.8, "2", FS, color=RED)
    t(x - 5.6, ytop + 3.6, "3 +", FS, "start", BLUE); t(x - 5.6, ybot - 2, "4 −", FS, "start", BLUE)
    pin(x - 7, ytop + 2.6); pin(x - 7, ybot - 3)
    return (x - 7, ytop + 2.6), (x - 7, ybot - 3)


def xt_r(x, y, label, pos):
    term(x, y, label, pos, RED)


# --- ТЭН
HT, HB = 206, 228
t(SX, HT - 17, "ТЭН", FSS, weight="bold")
t(412, HT - 1.3, "QF2", FS, "end", RED, "bold")
ln([(413, HT), (402.9, HT)], RED, arrow=True)
xt_r(402, HT, "XT4.14", "below")
thermo(378, HT, 19, ["T +100 °C", "руч. сброс"])
xt_r(372, HT, "XT4.15", "below")
thermo(348, HT, 19, ["T +50 °C", "автовозвр."])
xt_r(342, HT, "XT4.16", "below")
ln([(402, HT), (397, HT)], RED); ln([(378, HT), (372, HT)], RED); ln([(348, HT), (342, HT)], RED)
SSRX = 322
ln([(342, HT), (SSRX + 7.8, HT)], RED, arrow=True)
s1p, s1m = ssr_box(SSRX, HT, HB, "SSR1")
ln([(SSRX + 7, HB), (339.1, HB)], RED, arrow=True)
xt_r(340, HB, "XT4.17", "below")
box(346, HB - 4, 16, 8, ["ТЭН"], dash=True, stroke=GREY, size=FSS)
ln([(340, HB), (346, HB)], RED); ln([(362, HB), (367.1, HB)], RED, arrow=True)
xt_r(368, HB, "XT4.18", "below"); ln([(368, HB), (375, HB)], RED, arrow=True); stub(376, HB, "N")
xt_r(388, HB, "XT4.19", "below"); ln([(388, HB), (395, HB)], RED, arrow=True); stub(396, HB, "PE")
for n, w in ((15, "+100"), (16, "+50"), (17, "SSR1"), (18, "N"), (19, "PE")):
    xtok(n, w)
for x, kl, xn in ((372, "KL1", "X1"), (342, "KL2", "X5")):
    ln([(x, HT), (x, HT - 8.5)], RED)
    box(x - 10, HT - 15, 20, 6.5, [f"{kl} → {xn}"], stroke=RED)
    stub(x + 11, HT - 11.7, "N", RED)
t(SSRX - 6, HB + 10.5, "Показан вариант А (два термостата). Вариант Б — один общий провод:", FS, color="#a33", italic=True)
t(SSRX - 6, HB + 14.3, "выход цепи на XT4.15, перемычка XT4.15–16, KL1 на XT4.16,", FS, color="#a33", italic=True)
t(SSRX - 6, HB + 18.1, "KL2 не ставить, th50_enabled: \"false\" (panel_wiring.md 3.1).", FS, color="#a33", italic=True)

# --- преднагреватель
PT, PB = 264, 283
t(SSRX - 6, PT - 8, "Преднагреватель", FSS, weight="bold")
t(412, PT - 1.3, "QF3", FS, "end", RED, "bold")
ln([(413, PT), (402.9, PT)], RED, arrow=True)
xt_r(402, PT, "XT5.20", "below")
thermo(371, PT, 26, ["T преднагр.", "есть? уточнить"])
xt_r(366, PT, "XT5.21", "below")
ln([(402, PT), (397, PT)], RED); ln([(372, PT), (366, PT)], RED)
ln([(366, PT), (SSRX + 7.8, PT)], RED, arrow=True)
el.append(f'<path d="M399,{PT - 3} Q384.5,{PT - 10} 370,{PT - 3}" fill="none" stroke="{RED}" stroke-width="0.45" stroke-dasharray="1.2,0.9"/>')
t(384.5, PT - 9.5, "перемычка — временно", FS, "middle", RED)
s2p, s2m = ssr_box(SSRX, PT, PB, "SSR2")
ln([(SSRX + 7, PB), (339.1, PB)], RED, arrow=True)
xt_r(340, PB, "XT5.22", "below")
box(345, PB - 4, 29, 8, ["преднагреватель"], dash=True, stroke=GREY)
ln([(340, PB), (345, PB)], RED); ln([(374, PB), (379.1, PB)], RED, arrow=True)
xt_r(380, PB, "XT5.23", "below"); ln([(380, PB), (386, PB)], RED, arrow=True); stub(387, PB, "N")
xt_r(399, PB, "XT5.24", "below"); ln([(399, PB), (405, PB)], RED, arrow=True); stub(406, PB, "PE")
for n, w in ((21, "SSR2"), (22, "SSR2"), (23, "N"), (24, "PE")):
    xtok(n, w)
ln([(352, PT), (352, PT + 6)], RED, dash=True)
box(343, PT + 6, 18, 6, ["KL3 → X6"], dash=True, stroke=RED)
t(363, PT + 10, "по желанию", FS, color=RED)

# J4 → SSR1/SSR2 (3/4)
for jn, (px, py), lane in (("J4.1", s1p, 303), ("J4.2", s1m, 306), ("J4.3", s2p, 309), ("J4.4", s2m, 312)):
    y = JY[jn]
    ln([(RJ, y), (lane, y), (lane, py), (px - 0.8, py)], arrow=True)
jok("SSR1 клемма 3"); jok("SSR1 клемма 4"); jok("SSR2 клемма 3"); jok("SSR2 клемма 4")
t(RJ + 3, JY["J4.1"] - 1.3, "CH1: GPIO14 → SSR1 (ТЭН)", FS, color=BLUE, weight="bold")
t(RJ + 3, JY["J4.3"] - 1.3, "CH2: GPIO13 → SSR2 (преднагрев)", FS, color=BLUE, weight="bold")

# ================================================================ расхождения на рисунке
if problems:
    t(3, 240, "Не сошлось с panel_wiring.md:", FS, color="#a33", weight="bold")
    for i, p in enumerate(problems[:3]):
        t(3, 244 + i * 4, p[:55], FS, color="#a33")

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="420mm" height="297mm" viewBox="0 0 420 297"
     font-family="DejaVu Sans, Liberation Sans, sans-serif">
<defs>
  <marker id="ar" viewBox="0 0 6 6" refX="5.5" refY="3" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M0,0 L6,3 L0,6 z" fill="{RED}"/></marker>
  <marker id="ab" viewBox="0 0 6 6" refX="5.5" refY="3" markerWidth="5" markerHeight="5" orient="auto-start-reverse"><path d="M0,0 L6,3 L0,6 z" fill="{BLUE}"/></marker>
</defs>
<rect width="420" height="297" fill="#ffffff"/>
{chr(10).join(el)}
{chr(10).join(top)}
</svg>
'''
out_svg.write_text(svg, encoding="utf-8")

# ---------------------------------------------------------------- PDF (A3 альбомный)
if shutil.which("node"):
    with tempfile.TemporaryDirectory() as tmp:
        html = pathlib.Path(tmp) / "p.html"
        html.write_text("<!doctype html><meta charset=utf-8><style>@page{size:420mm 297mm;margin:0}"
                        "html,body{margin:0}svg{display:block}</style>" + svg, encoding="utf-8")
        js = pathlib.Path(tmp) / "p.js"
        js.write_text("const {chromium}=require('playwright');(async()=>{const b=await chromium.launch();"
                      "const p=await b.newPage();await p.goto('file://'+process.argv[2]);"
                      "await p.evaluate(()=>document.fonts.ready);"
                      "await p.pdf({path:process.argv[3],width:'420mm',height:'297mm',printBackground:true,"
                      "margin:{top:0,right:0,bottom:0,left:0}});await b.close();})()"
                      ".catch(e=>{console.error(e);process.exit(1)});", encoding="utf-8")
        env = dict(os.environ, NODE_PATH=os.environ.get("NODE_PATH", "/opt/node-tools/node_modules"))
        subprocess.run(["node", str(js), str(html), str(out_pdf)], check=True, env=env)
else:
    print("node не найден: PDF не собран", file=sys.stderr)

print(out_svg)
print(out_pdf)
print("не сошлось с таблицами:", len(problems))
for p in problems:
    print("  -", p)
