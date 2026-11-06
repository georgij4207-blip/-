# Раскладка самодельных модулей на макетке 5 × 7 см (отверстия 2,54 мм, сетка 24 × 18):
#   модуль датчиков (ADS1115 + делители) и модуль ключей SSR (PC817C + BS170).
# Для каждой детали — отверстия выводов, для каждой цепи — перемычки (голый провод по низу)
# и провода (изолированный ПВАМ 0,5). Скрипт ПРОВЕРЯЕТ раскладку по схеме:
#   - в отверстии только одна цепь; провод и перемычка не задевают чужие отверстия;
#   - все выводы каждой цепи соединены; лишних соединений нет.
# Схемы — docs/boards_assembly.md (листы 2 и 3), docs/ssr_driver.md.
# Выход: docs/макетки.svg (оба модуля), docs/макетки.pdf (A4 альбомный, масштаб 2,5 : 1).
#   python3 tools/build_perfboard.py
import base64, math, os, pathlib, subprocess, sys
from html import escape

root = pathlib.Path(__file__).resolve().parent.parent
P = 2.54          # шаг, мм
COLS, ROWS = 24, 18

class Board:
    def __init__(self, name, title):
        self.name, self.title = name, title
        self.parts = []      # (kind, ref, value, pins{name: (x,y)}, extra)
        self.pinnet = {}     # (x,y) -> net
        self.bare = []       # (net, [(x,y)...])
        self.ins = []        # (net, a, b)
        self.order = []      # порядок пайки

    def part(self, kind, ref, value, pins, nets, extra=None):
        for pn, xy in pins.items():
            net = nets[pn]
            if xy in self.pinnet:
                sys.exit(f"{self.name}: отверстие {xy} занято дважды ({ref}.{pn})")
            self.pinnet[xy] = net
        self.parts.append((kind, ref, value, pins, nets, extra or {}))

    # --- посадочные места
    def R(self, ref, value, a, b, na, nb):
        self.part("R", ref, value, {"1": a, "2": b}, {"1": na, "2": nb})
    def C(self, ref, value, a, b, na, nb):
        self.part("C", ref, value, {"1": a, "2": b}, {"1": na, "2": nb})
    def PTC(self, ref, value, a, b, na, nb):
        self.part("PTC", ref, value, {"1": a, "2": b}, {"1": na, "2": nb})
    def DIP4(self, ref, value, x, y, n1, n2, n3, n4):
        # вид сверху, точка у вывода 1: 1 и 2 — левый ряд (сверху вниз), 4 и 3 — правый ряд
        self.part("DIP4", ref, value, {"1": (x, y), "2": (x, y + 1), "3": (x + 3, y + 1), "4": (x + 3, y)},
                  {"1": n1, "2": n2, "3": n3, "4": n4})
    def TO92(self, ref, value, x, y, order, nets):
        pins = {p: (x + i, y) for i, p in enumerate(order)}
        self.part("TO92", ref, value, pins, nets, {"order": order})
    def LED(self, ref, value, a, k, na, nk):
        self.part("LED", ref, value, {"A": a, "K": k}, {"A": na, "K": nk})
    def TERM(self, ref, x, y, labels, nets, side="left"):
        pins = {str(i + 1): (x, y + i) for i in range(len(labels))}
        self.part("TERM", ref, "", pins, {str(i + 1): n for i, n in enumerate(nets)},
                  {"labels": labels, "side": side})
    def HEADER(self, ref, x, y, labels, nets, body):
        pins = {lab: (x, y + i) for i, lab in enumerate(labels)}
        self.part("HDR", ref, "ADS1115", pins, {lab: n for lab, n in zip(labels, nets)}, {"body": body})

    # --- соединения
    def jumper(self, net, *pts):     # голый провод по низу, по прямым через соседние отверстия
        path = [pts[0]]
        for p in pts[1:]:
            x0, y0 = path[-1]
            dx = (p[0] > x0) - (p[0] < x0); dy = (p[1] > y0) - (p[1] < y0)
            if dx and dy:
                sys.exit(f"{self.name}: перемычка {net} не по прямой: {path[-1]} → {p}")
            while path[-1] != p:
                path.append((path[-1][0] + dx, path[-1][1] + dy))
        self.bare.append((net, path))
    def wire(self, net, a, b):
        self.ins.append((net, a, b))

    # --- проверка
    def check(self):
        occ = dict(self.pinnet)
        errs = []
        for net, path in self.bare:
            for h in path:
                if not (1 <= h[0] <= COLS and 1 <= h[1] <= ROWS):
                    errs.append(f"перемычка {net} вне платы: {h}")
                if occ.get(h, net) != net:
                    errs.append(f"перемычка {net} задевает {occ[h]} в {h}")
                occ[h] = net
        for net, a, b in self.ins:
            for h in (a, b):
                if occ.get(h) is None:
                    errs.append(f"провод {net}: конец {h} в пустом отверстии")
                elif occ[h] != net:
                    errs.append(f"провод {net}: конец {h} на цепи {occ[h]}")
        # связность
        parent = {}
        def f(x):
            parent.setdefault(x, x)
            while parent[x] != x:
                parent[x] = parent[parent[x]]; x = parent[x]
            return x
        def u(a, b): parent[f(a)] = f(b)
        for net, path in self.bare:
            for a, b in zip(path, path[1:]): u(a, b)
            f(path[0])
        for net, a, b in self.ins: u(a, b)
        nets = {}
        for h, n in self.pinnet.items(): nets.setdefault(n, []).append(h)
        for n, hs in nets.items():
            roots = {f(h) for h in hs}
            if len(roots) > 1:
                errs.append(f"цепь {n} не соединена: {len(roots)} частей")
        if errs:
            sys.exit(f"{self.name}: ОШИБКИ РАСКЛАДКИ:\n  " + "\n  ".join(errs))
        return nets

# ============================================================ модуль датчиков
S = Board("sens", "Модуль датчиков: ADS1115 + делители")
S.TERM("X11", 2, 3, ["3V", "GND", "SCL", "SDA"], ["3V", "GND", "SCL", "SDA"])
S.TERM("X12", 2, 9, ["вытяжка", "GND", "выброс", "GND", "улица", "GND"],
       ["A0", "GND", "A1", "GND", "A2", "GND"])
S.HEADER("A1", 18, 3, ["VDD", "GND", "SCL", "SDA", "ADDR", "ALRT", "A0", "A1", "A2", "A3"],
         ["3V", "GND", "SCL", "SDA", "GND", "NC", "A0", "A1", "A2", "A3"], body=(18, 2, 7, 12))
for i, (ch, rv) in enumerate([("A0", "22k 1%"), ("A1", "22k 1%"), ("A2", "22k 1%"), ("A3", "1k 1%")]):
    y = 9 + 2 * i
    S.R(f"R1{i+1}", rv, (6, y), (10, y), "3V", ch)
    S.C(f"C1{i+1}", "100n", (11, y), (13, y), ch, "GND")
S.jumper("3V", (2, 3), (18, 3))
S.jumper("GND", (2, 4), (18, 4))
S.jumper("SCL", (2, 5), (18, 5))
S.jumper("SDA", (2, 6), (18, 6))
S.jumper("3V", (6, 9), (6, 15))
S.jumper("GND", (13, 9), (13, 15))
for y in (9, 11, 13, 15):
    S.jumper({9: "A0", 11: "A1", 13: "A2", 15: "A3"}[y], (10, y), (11, y))
S.jumper("GND", (2, 10), (3, 10), (3, 14), (2, 14))
S.jumper("GND", (2, 12), (3, 12))
S.wire("3V", (6, 10), (6, 3))
S.wire("GND", (13, 10), (13, 4))
S.wire("GND", (3, 11), (13, 12))
S.wire("GND", (18, 7), (17, 4))
S.wire("A0", (2, 9), (10, 9))
S.wire("A1", (2, 11), (10, 11))
S.wire("A2", (2, 13), (10, 13))
S.wire("A0", (11, 9), (18, 9))
S.wire("A1", (11, 11), (18, 10))
S.wire("A2", (11, 13), (18, 11))
S.wire("A3", (11, 15), (18, 12))
S.order = [
    "Перемычки по низу: четыре шины сверху (ряды 3–6: 3V, GND, SCL, SDA от X11 до штырей ADS1115).",
    "Штыри под ADS1115 (колонка 18, ряды 3–12). Сам модуль — в конце.",
    "Резисторы R11–R13 (22 кОм 1 %) и R14 (1 кОм 1 %): левый вывод — колонка 6, правый — 10.",
    "Конденсаторы C11–C14 (100 нФ): колонки 11–13. Перемычка 10–11 в каждом ряду.",
    "Шина 3V по колонке 6 и шина GND по колонке 13 (голый провод через выводы).",
    "Клеммы X11 и X12 у левого края; у X12 — перемычки GND по колонке 3.",
    "Изолированные провода по таблице: 3V и GND к шинам, ADDR на GND, датчики, входы A0–A3.",
    "Проверка мультиметром (лист 2): 3V–GND не КЗ; 3V–A0/A1/A2 = 22 кОм; 3V–A3 = 1 кОм.",
    "Модуль ADS1115 на штыри. Выводы сверить по надписям на модуле!",
]

# ============================================================ модуль ключей SSR
K = Board("keys", "Модуль ключей SSR и контроль потока")
K.TERM("J1", 2, 3, ["+3V3", "GND", "CH1", "CH2"], ["3V3", "GND", "CH1", "CH2"])
K.TERM("J2", 2, 15, ["+12", "GND"], ["P12", "GND"])
K.TERM("J3", 23, 2, ["ДР COM", "ДР NO"], ["DPS", "FLOW"], side="right")
K.TERM("J5", 23, 6, ["X7", "GND вх."], ["X7", "E3"], side="right")
K.TERM("J4", 23, 11, ["SSR1 +", "SSR1 −", "SSR2 +", "SSR2 −"], ["FLOW", "D1", "FLOW", "D2"], side="right")
K.PTC("F1", "RXEF050", (19, 2), (21, 2), "P12", "DPS")
K.R("R7", "2k2", (8, 2), (12, 2), "FLOW", "U3A")
K.DIP4("U3", "PC817C", 13, 2, "U3A", "GND", "E3", "X7")
for ch, dy, ref in ((1, 0, ("R1", "U1", "R2", "R3", "Q1", "R4", "LED1")),
                    (2, 5, ("R5", "U2", "R6", "R8", "Q2", "R9", "LED2"))):
    rin, u, rg, rpd, q, rl, led = ref
    k, e, g, d, la = f"K{ch}", f"E{ch}", f"G{ch}", f"D{ch}", f"LA{ch}"
    K.R(rin, "330", (4, 8 + dy), (8, 8 + dy), f"CH{ch}", k)
    K.DIP4(u, "PC817C", 9, 7 + dy, "3V3", k, e, "FLOW")
    K.R(rg, "100", (13, 8 + dy), (17, 8 + dy), e, g)
    K.R(rpd, "10k", (12, 9 + dy), (16, 9 + dy), e, "GND")
    K.TO92(q, "BS170", 18, 9 + dy, ["S", "G", "D"], {"S": "GND", "G": g, "D": d})
    K.R(rl, "2k2", (16, 6 + dy), (20, 6 + dy), "FLOW", la)
    K.LED(led, "5 мм", (21, 6 + dy), (21, 7 + dy), la, d)
    K.jumper(k, (8, 8 + dy), (9, 8 + dy))
    K.jumper(e, (12, 8 + dy), (13, 8 + dy))
    K.jumper(e, (12, 8 + dy), (12, 9 + dy))
    K.jumper("FLOW", (12, 7 + dy), (14, 7 + dy))
    K.jumper("FLOW", (14, 6 + dy), (16, 6 + dy))
    K.jumper(la, (20, 6 + dy), (21, 6 + dy))
    K.wire(f"CH{ch}", (2, 4 + ch), (4, 8 + dy))
    K.wire(g, (17, 8 + dy), (19, 9 + dy))
    K.wire(d, (21, 7 + dy), (20, 9 + dy))
    K.wire(d, (20, 9 + dy), (23, 11 + 2 * (ch - 1) + 1))
K.jumper("DPS", (21, 2), (23, 2))
K.jumper("U3A", (12, 2), (13, 2))
K.jumper("FLOW", (14, 4), (14, 12))
K.jumper("GND", (2, 16), (2, 17), (21, 17))
K.jumper("GND", (16, 14), (16, 17))
K.jumper("GND", (18, 14), (18, 17))
K.wire("P12", (2, 15), (19, 2))
K.wire("FLOW", (23, 3), (14, 5))
K.wire("FLOW", (8, 2), (14, 4))
K.wire("FLOW", (14, 10), (23, 11))
K.wire("FLOW", (14, 9), (23, 13))
K.wire("3V3", (2, 3), (9, 7))
K.wire("3V3", (9, 7), (9, 12))
K.wire("GND", (2, 4), (3, 17))
K.wire("GND", (13, 3), (13, 17))
K.wire("GND", (16, 9), (17, 17))
K.wire("GND", (18, 9), (19, 17))
K.wire("X7", (16, 2), (23, 6))
K.wire("E3", (16, 3), (23, 7))
K.order = [
    "Клеммы J1, J2 (левый край) и J3, J5, J4 (правый край).",
    "Шина GND по низу: ряд 17 от J2 до колонки 21.",
    "F1 (колонки 19–21, ряд 2) и перемычка к J3.1. Шина +12V_FLOW по колонке 14 (ряды 4–12).",
    "Половинки панелек DIP-8 под U1, U2, U3 — точка (вывод 1) слева сверху.",
    "Резисторы: R7 2,2 кОм; R1, R5 330 Ом; R2, R6 100 Ом; R3, R8 10 кОм; R4, R9 2,2 кОм.",
    "Транзисторы Q1, Q2 (BS170) плоской стороной ОТ себя: слева направо исток, затвор, сток.",
    "Светодиоды LED1, LED2: длинная ножка (анод) — вверху (ряды 6 и 11).",
    "Короткие перемычки у каждой детали (по таблице), потом изолированные провода.",
    "Проверка без оптронов: +12–GND не КЗ; потом по листу 3 — с 12 В, перемычкой «поток» и 3,3 В.",
]

boards = [S, K]
for b in boards:
    b.check()
    print(f"{b.name}: раскладка проверена — деталей {len(b.parts)}, перемычек {len(b.bare)}, проводов {len(b.ins)}")

# ============================================================ рисование
NETCOL = {"3V": "#c0392b", "3V3": "#c0392b", "P12": "#d35400", "DPS": "#d35400", "FLOW": "#e67e22",
          "GND": "#2c3e50", "SDA": "#2471a3", "SCL": "#7d3c98", "A0": "#117a65", "A1": "#1e8449",
          "A2": "#239b56", "A3": "#7e5109", "X7": "#2471a3", "E3": "#5d6d7e"}
def col(net): return NETCOL.get(net, "#1f618d")

def svg_board(b, scale):
    M = 6  # поле, мм
    W, H = (COLS + 1) * P + 2 * M + 30, (ROWS + 1) * P + 2 * M
    def X(x): return M + 15 + x * P
    def Y(y): return M + y * P
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:.1f} {H:.1f}" width="{W*scale:.0f}mm" '
         f'height="{H*scale:.0f}mm" font-family="DejaVu Sans, sans-serif">']
    o.append(f'<rect x="{X(0.3):.2f}" y="{Y(0.3):.2f}" width="{(COLS+0.4)*P:.2f}" height="{(ROWS+0.4)*P:.2f}" '
             f'rx="1.5" fill="#e8f0d8" stroke="#6b8e23" stroke-width="0.4"/>')
    for x in range(1, COLS + 1):
        o.append(f'<text x="{X(x):.2f}" y="{Y(0.05):.2f}" font-size="1.3" text-anchor="middle" fill="#555">{x}</text>')
    for y in range(1, ROWS + 1):
        o.append(f'<text x="{X(0.25):.2f}" y="{Y(y)+0.45:.2f}" font-size="1.3" text-anchor="end" fill="#555">{y}</text>')
    for x in range(1, COLS + 1):
        for y in range(1, ROWS + 1):
            o.append(f'<circle cx="{X(x):.2f}" cy="{Y(y):.2f}" r="0.55" fill="#c9a227" stroke="#8a6d10" stroke-width="0.12"/>')
    # перемычки (по низу)
    for net, path in b.bare:
        pts = " ".join(f"{X(x):.2f},{Y(y):.2f}" for x, y in path)
        o.append(f'<polyline points="{pts}" fill="none" stroke="#9aa3a8" stroke-width="0.9" stroke-linecap="round" stroke-linejoin="round" opacity="0.95"/>')
    # детали
    for kind, ref, value, pins, nets, ex in b.parts:
        if kind in ("R", "C", "PTC"):
            (x1, y1), (x2, y2) = pins["1"], pins["2"]
            cx, cy = (X(x1) + X(x2)) / 2, (Y(y1) + Y(y2)) / 2
            L = math.hypot(X(x2) - X(x1), Y(y2) - Y(y1))
            ang = math.degrees(math.atan2(Y(y2) - Y(y1), X(x2) - X(x1)))
            o.append(f'<line x1="{X(x1):.2f}" y1="{Y(y1):.2f}" x2="{X(x2):.2f}" y2="{Y(y2):.2f}" stroke="#666" stroke-width="0.35"/>')
            if kind == "R":
                bw, bh, fill = min(L - 1.6, 6.3), 2.2, "#d9c7a0"
            elif kind == "C":
                bw, bh, fill = 3.0, 2.2, "#e0a64a"
            else:
                bw, bh, fill = 4.2, 3.0, "#d4ac0d"
            o.append(f'<g transform="translate({cx:.2f},{cy:.2f}) rotate({ang:.1f})"><rect x="{-bw/2:.2f}" y="{-bh/2:.2f}" '
                     f'width="{bw:.2f}" height="{bh:.2f}" rx="0.8" fill="{fill}" stroke="#555" stroke-width="0.2"/></g>')
            o.append(f'<text x="{cx:.2f}" y="{cy-1.45:.2f}" font-size="1.35" text-anchor="middle"><tspan font-weight="bold">{ref}</tspan> {escape(value)}</text>')
        elif kind == "DIP4":
            (x1, y1) = pins["1"]
            o.append(f'<rect x="{X(x1)-0.9:.2f}" y="{Y(y1)-1.1:.2f}" width="{3*P+1.8:.2f}" height="{P+2.2:.2f}" fill="#222" rx="0.4"/>')
            o.append(f'<circle cx="{X(x1)+0.9:.2f}" cy="{Y(y1)-0.2:.2f}" r="0.45" fill="#fff"/>')
            o.append(f'<text x="{X(x1)+1.5*P:.2f}" y="{Y(y1)+0.6*P:.2f}" font-size="1.5" fill="#fff" text-anchor="middle" font-weight="bold">{ref}</text>')
            for pn, (x, y) in pins.items():
                dx = -1.6 if pn in "12" else 1.6
                o.append(f'<text x="{X(x)+dx:.2f}" y="{Y(y)+0.5:.2f}" font-size="1.1" fill="#fff" text-anchor="middle">{pn}</text>')
        elif kind == "TO92":
            xs = [X(x) for x, _ in pins.values()]; y0 = Y(list(pins.values())[0][1])
            cx = sum(xs) / 3
            o.append(f'<path d="M{cx-2.6:.2f},{y0-0.6:.2f} H{cx+2.6:.2f} A2.6,2.6 0 0 1 {cx-2.6:.2f},{y0-0.6:.2f} Z" fill="#222" fill-opacity="0.85"/>')
            o.append(f'<line x1="{cx-2.6:.2f}" y1="{y0-0.6:.2f}" x2="{cx+2.6:.2f}" y2="{y0-0.6:.2f}" stroke="#c0392b" stroke-width="0.4"/>')
            o.append(f'<text x="{cx-3.2:.2f}" y="{y0+0.5:.2f}" font-size="1.5" text-anchor="end" font-weight="bold">{ref}</text>')
            for pn, (x, y) in pins.items():
                o.append(f'<text x="{X(x):.2f}" y="{Y(y)+2.3:.2f}" font-size="1.3" text-anchor="middle" font-weight="bold" fill="#c0392b">{pn}</text>')
        elif kind == "LED":
            (xa, ya), (xk, yk) = pins["A"], pins["K"]
            cx, cy = (X(xa) + X(xk)) / 2, (Y(ya) + Y(yk)) / 2
            o.append(f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="2.6" fill="#5dade2" fill-opacity="0.45" stroke="#1f618d" stroke-width="0.3"/>')
            o.append(f'<text x="{X(xa)+1.3:.2f}" y="{Y(ya)+0.5:.2f}" font-size="1.2" font-weight="bold">A+</text>')
            o.append(f'<text x="{X(xk)+1.3:.2f}" y="{Y(yk)+0.5:.2f}" font-size="1.2">K</text>')
            o.append(f'<text x="{cx+3.0:.2f}" y="{cy+0.5:.2f}" font-size="1.4" font-weight="bold">{ref}</text>')
        elif kind == "TERM":
            ys = [y for _, y in pins.values()]; x0 = list(pins.values())[0][0]
            left = ex["side"] == "left"
            bx = X(x0) - 1.4 - (4.2 if left else -1.4) + (0 if left else -1.4)
            bx = X(x0) - 5.6 if left else X(x0) - 1.4
            o.append(f'<rect x="{bx:.2f}" y="{Y(min(ys))-1.27:.2f}" width="7.0" height="{len(ys)*P:.2f}" fill="#2e86c1" fill-opacity="0.85" rx="0.4"/>')
            for (pn, (x, y)), lab in zip(pins.items(), ex["labels"]):
                o.append(f'<circle cx="{X(x)+(-2.6 if left else 2.6):.2f}" cy="{Y(y):.2f}" r="0.9" fill="#d6eaf8"/>')
                tx = X(x) - 7.0 if left else X(x) + 6.4
                o.append(f'<text x="{tx:.2f}" y="{Y(y)+0.5:.2f}" font-size="1.35" text-anchor="{"end" if left else "start"}">{ref}.{pn} {escape(lab)}</text>')
        elif kind == "HDR":
            bx, by, bw, bh = ex["body"]
            o.append(f'<rect x="{X(bx)-1.3:.2f}" y="{Y(by):.2f}" width="{bw*P:.2f}" height="{bh*P:.2f}" fill="#1a5276" fill-opacity="0.18" stroke="#1a5276" stroke-width="0.35" stroke-dasharray="1,0.6"/>')
            o.append(f'<text x="{X(bx)+bw*P/2:.2f}" y="{Y(by+bh/2):.2f}" font-size="1.7" text-anchor="middle" fill="#1a5276" font-weight="bold">модуль ADS1115</text>')
            o.append(f'<text x="{X(bx)+bw*P/2:.2f}" y="{Y(by+bh/2)+2.2:.2f}" font-size="1.2" text-anchor="middle" fill="#1a5276">сверху, на штырях</text>')
            for lab, (x, y) in pins.items():
                o.append(f'<rect x="{X(x)-0.8:.2f}" y="{Y(y)-0.8:.2f}" width="1.6" height="1.6" fill="#333"/>')
                o.append(f'<text x="{X(x)+1.4:.2f}" y="{Y(y)+0.45:.2f}" font-size="1.2" fill="#1a5276">{lab}</text>')
    # провода (изолированные, сверху)
    for i, (net, a, b2) in enumerate(b.ins):
        x1, y1, x2, y2 = X(a[0]), Y(a[1]), X(b2[0]), Y(b2[1])
        mx, my = (x1 + x2) / 2, (y1 + y2) / 2
        dx, dy = x2 - x1, y2 - y1; L = math.hypot(dx, dy) or 1
        bend = min(3.0, L * 0.12)
        cx, cy = mx - dy / L * bend, my + dx / L * bend
        o.append(f'<path d="M{x1:.2f},{y1:.2f} Q{cx:.2f},{cy:.2f} {x2:.2f},{y2:.2f}" fill="none" stroke="{col(net)}" '
                 f'stroke-width="0.55" stroke-linecap="round" opacity="0.9"/>')
        o.append(f'<circle cx="{x1:.2f}" cy="{y1:.2f}" r="0.5" fill="{col(net)}"/><circle cx="{x2:.2f}" cy="{y2:.2f}" r="0.5" fill="{col(net)}"/>')
        tx, ty = 0.25 * x1 + 0.5 * cx + 0.25 * x2, 0.25 * y1 + 0.5 * cy + 0.25 * y2
        o.append(f'<g><circle cx="{tx:.2f}" cy="{ty:.2f}" r="1.05" fill="#fff" stroke="{col(net)}" stroke-width="0.25"/>'
                 f'<text x="{tx:.2f}" y="{ty+0.45:.2f}" font-size="1.15" text-anchor="middle" font-weight="bold" fill="{col(net)}">{i+1}</text></g>')
    o.append("</svg>")
    return "\n".join(o)

def hole(h): return f"{h[0]}/{h[1]}"
def wires_table(b):
    rows = []
    for i, (net, a, c) in enumerate(b.ins):
        def what(h):
            for kind, ref, value, pins, nets, ex in b.parts:
                for pn, xy in pins.items():
                    if xy == h:
                        return f"{ref}.{pn}"
            return "шина"
        rows.append(f"<tr><td>{i+1}</td><td style='color:{col(net)};font-weight:bold'>{escape(net)}</td>"
                    f"<td>{what(a)} ({hole(a)})</td><td>{what(c)} ({hole(c)})</td></tr>")
    return ("<table><tr><th>№</th><th>Цепь</th><th>Откуда (колонка/ряд)</th><th>Куда</th></tr>" + "".join(rows) + "</table>")

def jumpers_table(b):
    rows = [f"<tr><td style='color:{col(n)};font-weight:bold'>{escape(n)}</td><td>{' → '.join(hole(h) for h in (p[0], p[-1]))}</td><td>{len(p)-1}</td></tr>"
            for n, p in b.bare]
    return ("<table><tr><th>Цепь</th><th>От — до (колонка/ряд)</th><th>Шагов</th></tr>" + "".join(rows) + "</table>")

NOTE = {"sens": "Сетка: колонки 1–24 слева направо, ряды 1–18 сверху вниз (вид сверху, со стороны деталей). "
                "Серые линии — голые перемычки по низу платы через выводы; цветные — изолированные провода ПВАМ 0,5 "
                "(номер — в таблице). Клеммы X11/X12 — у левого края, провода от платы и датчиков входят слева.",
        "keys": "Вид сверху. Серые линии — голые перемычки по низу; цветные — изолированные провода. "
                "Оптроны — в половинках панелек DIP-8 (панельку разрезать пополам), точка — вывод 1. "
                "У транзисторов красная линия — плоская сторона корпуса: она смотрит вверх (к ряду 1). "
                "Соседние отверстия разных цепей не замыкать каплей припоя: после пайки проверить мультиметром."}

svg_all = []
html = ["<!doctype html><meta charset='utf-8'><style>",
        "@page { size: A4 landscape; margin: 10mm; } body { font-family: 'DejaVu Sans', sans-serif; font-size: 10pt; color: #111; }",
        "h1 { font-size: 15pt; margin: 0 0 2mm; } h2 { font-size: 12pt; margin: 3mm 0 1mm; } p { margin: 1mm 0; }",
        ".sheet { break-after: page; } .sheet:last-child { break-after: auto; }",
        "table { border-collapse: collapse; font-size: 8.5pt; } td, th { border: 1px solid #aaa; padding: 1px 5px; text-align: left; }",
        ".cols { display: flex; gap: 6mm; align-items: flex-start; } ol { margin: 1mm 0; padding-left: 5mm; } li { margin: 0.5mm 0; }",
        "</style>"]
for b in boards:
    s = svg_board(b, 2.5)
    svg_all.append(s)
    html.append(f"<div class='sheet'><h1>{escape(b.title)} — раскладка на макетке 5 × 7 см (масштаб 2,5 : 1)</h1>"
                f"<p>{escape(NOTE[b.name])}</p>{s}</div>")
    html.append(f"<div class='sheet'><h1>{escape(b.title)} — порядок пайки, перемычки и провода</h1><div class='cols'>"
                f"<div><h2>Порядок</h2><ol>" + "".join(f"<li>{escape(t)}</li>" for t in b.order) + "</ol>"
                f"<h2>Перемычки (голый провод по низу)</h2>{jumpers_table(b)}</div>"
                f"<div><h2>Провода (изолированные)</h2>{wires_table(b)}</div></div></div>")
out_svg = root / "docs" / "макетки.svg"
out_html = root / "docs" / "макетки.html"
out_pdf = root / "docs" / "макетки.pdf"
out_svg.write_text("\n".join(svg_all), encoding="utf-8")
out_html.write_text("\n".join(html), encoding="utf-8")
js = f"""
const {{ chromium }} = require('playwright');
(async () => {{ const b = await chromium.launch(); const p = await b.newPage();
  await p.goto('file://' + {str(out_html)!r}); await p.waitForTimeout(300);
  await p.pdf({{ path: {str(out_pdf)!r}, format: 'A4', landscape: true, printBackground: true, preferCSSPageSize: true }});
  await b.close(); }})().catch(e => {{ console.error(e); process.exit(1); }});"""
env = dict(os.environ, NODE_PATH=os.environ.get("NODE_PATH", "/opt/node-tools/node_modules"))
subprocess.run(["node", "-e", js], check=True, env=env)
print(out_pdf)
