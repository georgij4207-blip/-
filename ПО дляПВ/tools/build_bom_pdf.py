# Список покупок одним PDF из docs/bom.md → docs/список_покупок.pdf
# Таблицы bom.md разбираются и сводятся к столбцам: позиция, номинал/модель, кол-во, для чего, где купить.
# Раздел E («если на объекте чего-то не окажется») и штатные датчики — в «Не покупать».
# Нужны: pandoc и Node.js с Playwright/Chromium (tools/html_to_pdf.js).
#   python3 tools/build_bom_pdf.py
import datetime, os, pathlib, re, shutil, subprocess, sys, tempfile

root = pathlib.Path(__file__).resolve().parent.parent
tools = root / "tools"
out = root / "docs" / "список_покупок.pdf"

for tool in ("pandoc", "node"):
    if not shutil.which(tool):
        sys.exit(f"не найден {tool}: установите его и повторите")

# Где купить — по разделу bom.md; уточнения по словам в позиции. Только тип магазина:
# цен в bom.md нет.
SHOP = {
    "A": "электротехника, DIN-модульное",
    "B": "вентоборудование / КИП",
    "C": "радиодетали",
    "D": "радиодетали",
    "D2": "модули для Arduino/ESP",
    "F": "инструмент",
}
SHOP_BY_WORD = [
    ("Твердотельное", "КИП и автоматика"),
    ("Радиатор", "КИП и автоматика / радиодетали"),
    ("Интерфейсное реле", "электротехника, автоматика"),
    ("RC-снаббер", "радиодетали / электротехника"),
    ("Блок питания", "электротехника, автоматика"),
    ("Кабель датчиков", "кабельная продукция"),
    ("Провод монтажный", "радиодетали"),
    ("SCD41", "радиодетали / модули"),
    ("Senseair", "радиодетали / модули"),
]

# Есть в установке — не покупать (в bom.md этих позиций нет: ставятся штатные).
INSTALLED = [
    ("ТЭН притока", "1,5 кВт, 230 В (≈ 33–36 Ом)", "1", "основной нагрев притока", "—"),
    ("Преднагреватель", "0,6 кВт, 230 В (≈ 85–90 Ом)", "1", "защита рекуператора от обмерзания", "—"),
    ("Моторы притока и вытяжки", "AC с отводами обмотки, 0,92 А, с рабочими конденсаторами", "2",
     "вентиляторы", "конденсатор — если окажется неисправен (по шильдику)"),
    ("Датчики температуры NTC", "штатные, предположительно 10 кОм B3380", "4",
     "улица, приток, вытяжка, выброс", "запасные 0 (+2) — того же типа, что покажет прозвонка"),
    ("Плата KinCony KC868-A8", "ESP32, 8 реле, 8 входов", "1", "контроллер", "уже заказана"),
]


def clean(s):
    return s.strip().replace("|", "\\|")


def parse(md):
    """[(раздел, заголовок, [ {столбец: значение} ])]"""
    sections, cur, rows, head = [], None, None, None
    for line in md.splitlines():
        m = re.match(r"^## ([A-Z]\d?)\. (.*)", line)
        if m:
            cur, rows, head = m.group(1), [], None
            sections.append((cur, m.group(2), rows))
            continue
        if cur and line.startswith("|"):
            cells = [c.strip() for c in line.strip().strip("|").split("|")]
            if head is None:
                head = cells
            elif not set("".join(cells)) <= set("-: "):
                rows.append(dict(zip(head, cells)))
        elif cur and not line.strip():
            head = None
    return sections


def col(r, *names):
    for n in names:
        for k, v in r.items():
            if k.startswith(n):
                return v
    return ""


def shop(sec, what):
    for word, s in SHOP_BY_WORD:
        if word in what:
            return s
    return SHOP.get(sec, "")


def optional(qty, note):
    return qty.startswith("0") or "по желанию" in note or "только если" in qty


bom = (root / "docs/bom.md").read_text(encoding="utf-8")
sections = parse(bom)
prices = re.search(r"\d\s*(₽|руб\.?\b|р\.|\$|€)", bom)

buy, tools_rows, not_buy = [], [], []
for sec, title, rows in sections:
    for r in rows:
        pos = col(r, "Поз.")
        what = col(r, "Что")
        name = what if pos in ("", "—") else f"{pos} — {what}"
        model = col(r, "Параметры", "Номинал")
        qty = col(r, "Кол-во")
        note = col(r, "Примечание")
        if sec == "F":
            tools_rows.append((what, "", "1", col(r, "Зачем"), "инструмент (можно взять на время)"))
        elif sec == "E":
            not_buy.append((what, model, "—", "штатное в установке", "купить, " + col(r, "Когда")))
        elif "штатные" in note:
            continue  # NTC — в INSTALLED
        else:
            if sec == "C" and not note:
                note = "модуль ключей SSR"
            if sec == "D" and not note:
                note = "делители NTC на ADS1115"
            buy.append((name, model, qty, note, shop(sec, what), optional(qty, note), sec, title))

today = datetime.date.today().isoformat()
commit = subprocess.run(["git", "-C", str(root), "rev-parse", "--short", "HEAD"],
                        capture_output=True, text=True).stdout.strip()
H = "| Позиция | Номинал или модель | Кол-во | Для чего | Где купить (ориентир по цене) |\n|---|---|---|---|---|"

L = ["Источник — `docs/bom.md`. Марки — примеры, подойдут аналоги с теми же параметрами. "
     "В скобках у количества — запас сверх нужного.", ""]
if not prices:
    L += ["> **Цен в `docs/bom.md` нет**, поэтому в столбце «где купить» указан только тип магазина, "
          "а сумма в итоге не посчитана.", ""]

L += ["## 1. Купить", ""]
last = None
for name, model, qty, note, sh, opt, sec, title in buy:
    if sec != last:
        L += ["", f"### {sec}. {title}", "", H]
        last = sec
    mark = " *(по желанию)*" if opt and "по желанию" not in note else ""
    L.append(f"| {clean(name)} | {clean(model)} | {clean(qty)} | {clean(note)}{mark} | {clean(sh)} |")

L += ["", "## 2. Инструменты для стенда и объекта", "", H]
L += [f"| {' | '.join(clean(c) for c in t)} |" for t in tools_rows]

L += ["", "## 3. Не покупать — уже есть в установке", "",
      "Штатное оборудование Salda RIS 400P используется как есть. Покупать — только если при прозвонке "
      "(`docs/site_survey.md`) позиции не окажется или она неисправна.", "",
      "| Позиция | Номинал или модель | Кол-во | Для чего | Когда всё-таки купить |", "|---|---|---|---|---|"]
L += [f"| {' | '.join(clean(c) for c in t)} |" for t in INSTALLED + not_buy]

n_req = sum(1 for b in buy if not b[5])
n_opt = len(buy) - n_req
L += ["", "## Итог", "",
      "| | Позиций |", "|---|---|",
      f"| Купить обязательно | {n_req} |",
      f"| Купить по желанию / по результатам стенда | {n_opt} |",
      f"| **Всего к покупке** | **{len(buy)}** |",
      f"| Инструменты | {len(tools_rows)} |",
      f"| Не покупать (есть в установке) | {len(INSTALLED) + len(not_buy)} |",
      "",
      "Примерная сумма: **не посчитана — цен в `docs/bom.md` нет.** "
      "Позиция — это строка списка (например, «резистор 330 Ом, 2 шт.» — одна позиция)."
      if not prices else ""]

with tempfile.TemporaryDirectory() as tmp:
    src = pathlib.Path(tmp) / "src.md"
    html = pathlib.Path(tmp) / "doc.html"
    src.write_text("\n".join(L) + "\n", encoding="utf-8")
    subprocess.run(["pandoc", str(src), "-f", "gfm+ascii_identifiers", "-t", "html5", "-s",
                    "--metadata", "title=Список покупок",
                    "--metadata", f"subtitle=Salda RIS 400P на KinCony KC868-A8 · из docs/bom.md · "
                                  f"коммит {commit} · {today}",
                    "--css", str(tools / "wiring_pdf.css"), "--css", str(tools / "bom_pdf.css"),
                    "--embed-resources", "-o", str(html)], check=True)
    env = dict(os.environ, NODE_PATH=os.environ.get("NODE_PATH", "/opt/node-tools/node_modules"))
    subprocess.run(["node", str(tools / "html_to_pdf.js"), str(html), str(out), "Список покупок"],
                   check=True, env=env)

print(out)
print(f"к покупке {len(buy)} (обязательно {n_req}, по желанию {n_opt}), инструменты {len(tools_rows)}, "
      f"не покупать {len(INSTALLED) + len(not_buy)}")
