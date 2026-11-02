# Раскладка автоматики на монтажной пластине электроотсека Salda RIS 400P (внутри установки).
# Масштаб 1:1: координаты в мм, начало — левый верхний угол пластины 300 × 490 мм.
# Заводское (ввод, автотрансформатор, конденсаторы, пучок проводов) — по фото, точность ±10 мм.
# Выход: docs/раскладка_отсека.svg и docs/раскладка_отсека.pdf (лист 460 × 540 мм, 1:1).
#   python3 tools/build_in_unit_layout.py
import os, pathlib, subprocess, sys
from html import escape

root = pathlib.Path(__file__).resolve().parent.parent
svg_out = root / "docs" / "раскладка_отсека.svg"
pdf_out = root / "docs" / "раскладка_отсека.pdf"

PW, PH = 300, 490          # пластина
OX, OY = 20, 30            # отступ пластины на листе
W, H = 460, 540            # лист

FACTORY = "#d9d9d9"; NEW230 = "#f4c7a1"; NEWLV = "#b9d7f0"; HEAT = "#f1e3a0"
items, labels = [], []

def rect(x, y, w, h, fill, text="", dash=False, size=3.2):
    d = ' stroke-dasharray="3,2"' if dash else ""
    items.append(f'<rect x="{OX+x}" y="{OY+y}" width="{w}" height="{h}" fill="{fill}" stroke="#333" stroke-width="0.4"{d}/>')
    if text:
        lines = text.split("\n")
        for i, t in enumerate(lines):
            ty = OY + y + h / 2 + (i - (len(lines) - 1) / 2) * size * 1.25 + size * 0.35
            items.append(f'<text x="{OX+x+w/2}" y="{ty}" font-size="{size}" text-anchor="middle">{escape(t)}</text>')

def line(pts, color="#333", w=0.5, dash=False):
    d = ' stroke-dasharray="4,2"' if dash else ""
    p = " ".join(f"{OX+x},{OY+y}" for x, y in pts)
    items.append(f'<polyline points="{p}" fill="none" stroke="{color}" stroke-width="{w}"{d}/>')

# --- пластина
items.append(f'<rect x="{OX}" y="{OY}" width="{PW}" height="{PH}" fill="#fafafa" stroke="#000" stroke-width="0.8"/>')

# --- заводское (не трогаем)
rect(45, 65, 60, 40, FACTORY, "Ввод L N PE\n(заводской)", True)
rect(165, 5, 130, 110, FACTORY, "Автотрансформатор\nPOT 460 ВА\n(заводской)", True)
rect(170, 118, 125, 35, FACTORY, "Клеммы отводов\n0/80/120/140/170/230 В", True, 3.0)
rect(55, 160, 245, 35, FACTORY, "Пучок проводов (заводской жгут)", True)
rect(8, 215, 50, 130, FACTORY, "C1\nмотор\nпритока", True)
rect(64, 215, 52, 130, FACTORY, "C2\nмотор\nвытяжки", True)

# --- 230 В: DIN-рейка
rect(128, 205, 160, 90, "none")
items.append(f'<rect x="{OX+128}" y="{OY+246}" width="160" height="7.5" fill="#999"/>')
x = 130
rect(x, 207, 8, 86, "#888"); x += 9
for name, grp in [("KM1", 0), ("KM2", 0), ("KM3", 0), ("KM4", 1), ("KM5", 1), ("KM6", 1), ("KM7", 2),
                  ("KL1", 3), ("KL2", 3), ("KL3", 3)]:
    rect(x, 207, 6.2, 86, NEW230)
    items.append(f'<text x="{OX+x+3.1}" y="{OY+290}" font-size="2.8" text-anchor="middle" transform="rotate(-90 {OX+x+3.1} {OY+290})">{name}</text>')
    x += 6.2
x += 1
rect(x, 207, 17.5, 86, NEW230); items.append(f'<text x="{OX+x+8.75}" y="{OY+250}" font-size="3" text-anchor="middle">F1</text>'); x += 18.5
for i in range(6):
    rect(x, 222, 5.2, 56, NEW230); x += 5.2
items.append(f'<text x="{OX+x-15.6}" y="{OY+218}" font-size="2.8" text-anchor="middle">XT 230 В</text>')
rect(x + 1, 207, 8, 86, "#888")
labels.append((128, 200, "Рейка 35 мм, 160 мм — только 230 В"))

# --- SSR на алюминиевой пластине
rect(140, 300, 150, 45, HEAT)
rect(150, 300, 58, 45, NEW230, "SSR1\nТЭН 1,5 кВт", size=3.0)
rect(222, 300, 58, 45, NEW230, "SSR2\nпреднагр.", size=3.0)
labels.append((140, 300, ""))

# --- слаботочка
rect(90, 352, 200, 110, NEWLV, "KinCony KC868-A8\n200 × 110\nреле K1–K8 и 12 В — верхний край\nвходы X1–X8, датчики — нижний край", size=3.6)
rect(10, 352, 70, 40, NEWLV, "БП 12 В\n≤ 70 × 40", size=3.0)
rect(15, 395, 55, 70, NEWLV, "Модуль\nключей SSR\n+ ADS1115\n(макетка\n5 × 7 см)", size=3.0)

# --- белый кабель: переложить к правому краю
line([(121, 195), (121, 200), (296, 200), (296, 470)], "#c00", 1.2, True)
items.append(f'<text x="{OX+304}" y="{OY+400}" font-size="3" fill="#c00" text-anchor="middle" transform="rotate(-90 {OX+304} {OY+400})">белый кабель — вдоль правого края</text>')

# --- гермовводы
rect(20, 470, 260, 18, FACTORY, "Гермовводы и провода снизу (датчики, пульт)", True)

# --- граница зон
line([(5, 349), (295, 349)], "#c00", 0.6, True)
items.append(f'<text x="{OX+5}" y="{OY+348}" font-size="3" fill="#c00">выше — 230 В, ниже — 12 В и датчики (зазор ≥ 10 мм между проводами)</text>')

# --- размеры
def dim_h(x1, x2, y, t):
    line([(x1, y), (x2, y)], "#555", 0.3)
    for xx in (x1, x2): line([(xx, y - 2), (xx, y + 2)], "#555", 0.3)
    items.append(f'<text x="{OX+(x1+x2)/2}" y="{OY+y-1.5}" font-size="3" text-anchor="middle" fill="#555">{t}</text>')
dim_h(0, 300, -8, "300 мм")
items.append(f'<text x="{OX-6}" y="{OY+245}" font-size="3" fill="#555" text-anchor="middle" transform="rotate(-90 {OX-6} {OY+245})">490 мм</text>')

# --- масштабная линейка
line([(0, 505), (100, 505)], "#000", 0.6)
for xx in range(0, 101, 10): line([(xx, 503), (xx, 507)], "#000", 0.3)
items.append(f'<text x="{OX+50}" y="{OY+501}" font-size="3" text-anchor="middle">100 мм — проверить масштаб печати (1:1)</text>')

# --- легенда
LX = OX + PW + 15
leg = [
    (FACTORY, "заводское — не трогаем (по фото, ±10 мм)"),
    (NEW230, "новое, 230 В"),
    (HEAT, "алюминий 3 мм под SSR (теплоотвод), термопаста"),
    (NEWLV, "новое, 12 В и датчики"),
]
ty = OY + 5
items.append(f'<text x="{LX}" y="{ty}" font-size="4.5" font-weight="bold">Раскладка отсека, 1:1</text>'); ty += 7
for c, t in leg:
    items.append(f'<rect x="{LX}" y="{ty-3.5}" width="8" height="5" fill="{c}" stroke="#333" stroke-width="0.3"/>')
    items.append(f'<text x="{LX+11}" y="{ty}" font-size="3.2">{escape(t)}</text>'); ty += 7
notes = [
    "Глубина отсека 100 мм: всё ниже 90 мм",
    "(рейка 7,5 + реле ≤ 82; SSR плоские).",
    "",
    "KM1–KM3 — ступени притока,",
    "KM4–KM6 — вытяжки: узкие реле 6,2 мм,",
    "CO, катушка 12 В DC; переключают отвод",
    "автотрансформатора на мотор, блокировка",
    "NC-контактами (docs/salda_in_unit.md).",
    "KM7 — заслонка. KL1–KL3 — контроль",
    "термостатов (катушка 230 В).",
    "F1 — предохранитель БП 12 В (0,5 А).",
    "XT 230 В — N и PE моторов, заслонки.",
    "",
    "SSR лежат на алюминиевой полосе:",
    "силовые клеммы вверх (к 230 В),",
    "клеммы управления вниз (к ключам).",
    "",
    "Белый кабель (заводской) освобождает",
    "место под плату — переложить к",
    "правому краю.",
    "",
    "Ввод, автотрансформатор, конденсаторы,",
    "жгут — на своих местах, как с завода.",
    "",
    "Сверить по месту: координаты заводских",
    "узлов сняты с фото (±10 мм).",
]
ty += 3
for t in notes:
    items.append(f'<text x="{LX}" y="{ty}" font-size="3.1">{escape(t)}</text>'); ty += 4.6

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}" '
       f'font-family="DejaVu Sans, sans-serif">\n<rect width="{W}" height="{H}" fill="#fff"/>\n'
       + "\n".join(items) + "\n</svg>\n")
svg_out.write_text(svg, encoding="utf-8")

js = f"""
const {{ chromium }} = require('playwright');
(async () => {{
  const b = await chromium.launch(); const p = await b.newPage();
  await p.goto('file://' + {str(svg_out)!r});
  await p.pdf({{ path: {str(pdf_out)!r}, width: '{W}mm', height: '{H}mm', printBackground: true,
                 margin: {{ top: 0, right: 0, bottom: 0, left: 0 }} }});
  await b.close();
}})().catch(e => {{ console.error(e); process.exit(1); }});
"""
env = dict(os.environ, NODE_PATH=os.environ.get("NODE_PATH", "/opt/node-tools/node_modules"))
subprocess.run(["node", "-e", js], check=True, env=env)
print(svg_out); print(pdf_out)
