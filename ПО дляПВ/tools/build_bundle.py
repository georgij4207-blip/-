import pathlib, subprocess, datetime
root = pathlib.Path(__file__).resolve().parent.parent
out = root / "щиток_и_прошивка.txt"
commit = subprocess.run(["git", "-C", str(root), "rev-parse", "--short", "HEAD"], capture_output=True, text=True).stdout.strip()
L = []
def h1(t): L.extend(["", "#" * 100, "#  " + t, "#" * 100, ""])
def h2(t): L.extend(["", "=" * 100, "=== " + t, "=" * 100])

fw_order = ["firmware/ris400p.yaml", "firmware/boards/kc868_a8.yaml", "firmware/profiles/salda_ris400p.yaml"]
fw_order += sorted(str(p.relative_to(root)) for p in (root / "firmware/packages/core").glob("*.yaml"))
for d in ("sensors", "drivers", "ui"):
    fw_order += sorted(str(p.relative_to(root)) for p in (root / f"firmware/packages/{d}").glob("*.yaml"))
fw_order += sorted(str(p.relative_to(root)) for p in (root / "firmware/include/vent").glob("*.h"))
fw_order += ["firmware/secrets.example.yaml"]

L += ["ЩИТОК И ПРОШИВКА — Salda RIS 400P на KinCony KC868-A8",
      f"Собрано из репозитория, коммит {commit}, {datetime.date.today().isoformat()}.",
      "Исходники — в папке «ПО дляПВ» (ветка ccr-a4d04de1-y27dcw). Этот файл — сводка для чтения/печати;",
      "собирать прошивку из папки firmware/ (шрифты DejaVu лежат в firmware/fonts/ — в этот файл не входят).",
      "",
      "СОДЕРЖАНИЕ",
      "  Часть 1. Схема подключения щитка",
      "           1.1 Щит, клеммник XT, силовая часть, 12 В, P19, датчики, входы, реле (docs/panel_wiring.md)",
      "           1.2 Модуль ключей SSR и цепь +12V_FLOW (docs/ssr_driver.md)",
      "           1.3 Экран и датчик CO2 на P19 (docs/display_co2.md, раздел «Подключение»)",
      "  Часть 2. Прошивка — все файлы по порядку сборки:"]
L += ["           " + f for f in fw_order]
L += ["  Часть 3. Список мест «УТОЧНИТЬ»"]

h1("ЧАСТЬ 1. СХЕМА ПОДКЛЮЧЕНИЯ ЩИТКА")
h2("1.1  docs/panel_wiring.md")
L += (root / "docs/panel_wiring.md").read_text().splitlines()
h2("1.2  docs/ssr_driver.md — модуль ключей SSR (разделы «Схема», «Номиналы», «Реле перепада давления»)")
s = (root / "docs/ssr_driver.md").read_text()
a = s.index("## Схема"); b = s.index("## Поведение во всех состояниях")
L += s[a:b].splitlines()
h2("1.3  docs/display_co2.md — подключение экрана и CO2")
s = (root / "docs/display_co2.md").read_text()
a = s.index("## Подключение"); b = s.index("## Экран")
L += s[a:b].splitlines()

h1("ЧАСТЬ 2. ПРОШИВКА (все YAML и заголовки C++)")
for f in fw_order:
    h2(f)
    L += (root / f).read_text().rstrip("\n").splitlines()

h1("ЧАСТЬ 3. СПИСОК МЕСТ «УТОЧНИТЬ»")
L += (pathlib.Path(__file__).parent / "utochnit.txt").read_text().splitlines()
out.write_text("\n".join(L) + "\n")
print(out, len(L), "строк", out.stat().st_size, "байт")
