# Проверка docs/схема_щитка.pdf против исходников (docs/panel_wiring.md, docs/ssr_driver.md) и прошивки.
#  1. Текст PDF: нет слов с удвоенными буквами («ССххееммаа»), нет повторённых подряд строк.
#  2. Полнота: каждая ячейка таблиц и каждая строка схем из исходников есть в PDF (без учёта пробелов
#     и переносов) — значит, ничего не обрезано и не перемешано.
#  3. Клеммы XT1–XT8: все номера из таблицы клеммника есть в PDF.
#  4. XT7 по прошивке: таблица «датчик — канал — клеммы — пин» из profiles/salda_ris400p.yaml и
#     boards/kc868_a8.yaml сверяется с таблицей panel_wiring.md.
# Печатает первые 40 строк текста PDF и все клеммы XT. Код возврата ≠ 0 при любой ошибке.
#   python3 tools/check_wiring_pdf.py
import pathlib, re, subprocess, sys

root = pathlib.Path(__file__).resolve().parent.parent
pdf = root / "docs" / "схема_щитка.pdf"
sources = [root / "docs/panel_wiring.md", root / "docs/ssr_driver.md"]
profile = (root / "firmware/profiles/salda_ris400p.yaml").read_text(encoding="utf-8")
board = (root / "firmware/boards/kc868_a8.yaml").read_text(encoding="utf-8")
panel = sources[0].read_text(encoding="utf-8")
errors = []

text = subprocess.run(["pdftotext", "-layout", str(pdf), "-"], capture_output=True, text=True).stdout
raw = subprocess.run(["pdftotext", "-raw", str(pdf), "-"], capture_output=True, text=True).stdout
lines = [l.rstrip() for l in text.splitlines()]


def squash(s):
    # U+FE0E/FE0F: pandoc ставит селектор варианта после «↔», чтобы не было эмодзи — не текст
    return re.sub(r"[\s\ufe0e\ufe0f]+", "", s)


def md_plain(s):
    s = re.sub(r"`([^`]*)`", r"\1", s)
    s = s.replace("**", "").replace("\\|", "|")
    s = re.sub(r"(?<!\w)\*([^*]+)\*(?!\w)", r"\1", s)
    s = re.sub(r"<sub>(.*?)</sub>", r"\1", s)
    s = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", s)
    return s.strip()


pdf_sq = squash(raw) + squash(text)

# 1. удвоенные буквы и повторённые строки
fonts = subprocess.run(["pdffonts", str(pdf)], capture_output=True, text=True).stdout
if "Type 3" in fonts:
    errors.append("в PDF есть шрифты Type 3 (обведённый текст выводится дважды)")
for w in re.findall(r"\w{4,}", text):
    if len(w) % 2 == 0 and w[0::2] == w[1::2] and not w.isdigit():
        errors.append(f"удвоенные буквы: «{w}»")
prev = None
for l in lines:
    s = l.strip()
    if s and s == prev and len(s) > 12:
        errors.append(f"строка повторена подряд: «{s[:60]}»")
    prev = s

# 2. полнота: ячейки таблиц и строки схем
missing = 0
for src in sources:
    in_code = False
    for line in src.read_text(encoding="utf-8").splitlines():
        if line.startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            items = [line]
        elif line.startswith("|") and not set(line) <= set("|-: "):
            items = [c for c in re.split(r"(?<!\\)\|", line.strip().strip("|"))]
        else:
            continue
        for it in items:
            sq = squash(md_plain(it) if not in_code else it)
            if sq and sq not in pdf_sq:
                missing += 1
                if missing <= 15:
                    errors.append(f"{src.name}: нет в PDF: «{it.strip()[:70]}»")
if missing > 15:
    errors.append(f"… и ещё {missing - 15} фрагментов")

# 3. клеммы XT из таблицы клеммника
xt = {}
cur = None
for line in panel[panel.index("## 2. Клеммник"):panel.index("## 3. Силовая")].splitlines():
    c = [x.strip() for x in line.strip().strip("|").split("|")]
    if len(c) == 4 and re.match(r"^[\d,–\s]+$", c[1] or "x"):
        if c[0]:
            cur = re.match(r"XT\d", c[0]).group(0)
        nums = []
        for part in re.split(r",\s*", c[1]):
            if "–" in part:
                a, b = part.split("–"); nums += range(int(a), int(b) + 1)
            elif part.strip():
                nums.append(int(part))
        for n in nums:
            xt[n] = (cur, md_plain(c[2]))
expect = list(range(1, 48))
for n in expect:
    if n not in xt:
        errors.append(f"клемма {n} отсутствует в таблице клеммника panel_wiring.md")

# 4. XT7 по прошивке
def sub(name):
    m = re.search(rf"^\s*{name}:\s*\"?([^\"\s#]+)", profile, re.M)
    return m.group(1) if m else None


i2c = (re.search(r"i2c_sda:\s*(\S+)", board).group(1), re.search(r"i2c_scl:\s*(\S+)", board).group(1))
ads_addr = sub("ads1115_address")
ports = dict(re.findall(r"(\d) = (GPIO\d+)", re.search(r"Порты TEM/HUMI:.*", board).group(0)))
port_of = {v: k for k, v in ports.items()}
fw = []
for sensor in ("t_extract", "t_exhaust", "t_outdoor", "t_supply"):
    m = re.search(rf"^  {sensor}: !include\s*\n\s*file: \.\./packages/sensors/(\S+)\s*\n\s*vars: \{{(.*)\}}\s*$", profile, re.M)
    if not m:
        errors.append(f"прошивка: не найден пакет датчика {sensor}"); continue
    pkg, vars_ = m.group(1), m.group(2)
    if pkg == "ntc_ads1115.yaml":
        var = re.search(r"ads_channel: \"\$\{(\w+)\}\"", vars_).group(1)
        ch = sub(var).replace("_GND", "")
        fw.append((sensor, f"ADS1115 {ch} ({var}: {sub(var)})", ch, f"ADS1115 {ads_addr} по I2C: SDA {i2c[0]}, SCL {i2c[1]}"))
    else:
        var = re.search(r"adc_pin: \"\$\{(\w+)\}\"", vars_).group(1)
        gpio = sub(var)
        fw.append((sensor, f"{gpio} ({var})", gpio, f"порт TEM/HUMI {port_of.get(gpio, '?')}, DATA = {gpio}"))
doc_rows = {}
for line in panel.splitlines():
    m = re.match(r"\| (t_\w+) \| .*?\| (.*?) \| (.*?) \| (.*?) \|", line)
    if m:
        doc_rows[m.group(1)] = (md_plain(m.group(2)), md_plain(m.group(3)), md_plain(m.group(4)))
xt7 = {}
for n in range(28, 36):
    xt7.setdefault(xt[n][1].split()[0], []).append(n)
table = []
for sensor, chan, key, pin in fw:
    terms = xt7.get(sensor, [])
    term = f"{terms[0]} / {terms[1]}" if len(terms) == 2 else "уточнить"
    d = doc_rows.get(sensor)
    ok = d and key in d[0] and term.replace(" ", "") in d[1].replace(" ", "") and (key in d[2] or key.startswith("A"))
    table.append((sensor, chan, term, pin, "совпадает" if ok else "УТОЧНИТЬ"))
    if not ok:
        errors.append(f"XT7: {sensor} — в прошивке {chan}, в panel_wiring.md {d}")

# ---------------------------------------------------------------- вывод
print("=== Первые 40 строк текста PDF (pdftotext -layout)")
for i, l in enumerate(lines[:40], 1):
    print(f"{i:2} | {l}")
print("\n=== Клеммы XT1–XT8 (таблица panel_wiring.md; «в PDF» — найдено в тексте PDF)")
for n in expect:
    if n in xt:
        grp, what = xt[n]
        found = squash(what) in pdf_sq
        print(f"{grp}.{n:<3} {'в PDF' if found else 'НЕТ В PDF'} | {what}")
        if not found:
            errors.append(f"{grp}.{n}: строки нет в PDF")
print("\n=== XT7 по прошивке (profiles/salda_ris400p.yaml + boards/kc868_a8.yaml)")
print("датчик | канал (прошивка) | клеммы XT7 (сигнал / GND) | пин платы | panel_wiring.md")
for r in table:
    print(" | ".join(r))
print("\n=== Итог:", "ошибок нет" if not errors else f"ошибок {len(errors)}")
for e in errors:
    print("  -", e)
sys.exit(1 if errors else 0)
