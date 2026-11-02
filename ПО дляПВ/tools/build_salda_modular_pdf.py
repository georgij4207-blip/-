# Список покупок модульной сборки Salda RIS 400P одним PDF:
# docs/salda_modular.md (разделы 4 «Полный список покупок» и 5 «Что уточнить») → docs/список_покупок_модульная.pdf
# Нужны: pandoc и Node.js с Playwright/Chromium (tools/html_to_pdf.js).
#   python3 tools/build_salda_modular_pdf.py
import base64, datetime, os, pathlib, re, shutil, subprocess, sys, tempfile

root = pathlib.Path(__file__).resolve().parent.parent
tools = root / "tools"
src_md = root / "docs" / "salda_modular.md"
out = root / "docs" / "список_покупок_модульная.pdf"

for tool in ("pandoc", "node"):
    if not shutil.which(tool):
        sys.exit(f"не найден {tool}: установите его и повторите")

FONTS = {
    ("DejaVu Sans", "normal"): root / "firmware/fonts/DejaVuSans.ttf",
    ("DejaVu Sans", "bold"): root / "firmware/fonts/DejaVuSans-Bold.ttf",
}
for f in FONTS.values():
    if not f.exists():
        sys.exit(f"нет шрифта {f}")

md = src_md.read_text(encoding="utf-8")
m = re.search(r"^## 4\. .*?(?=\Z)", md, re.S | re.M)
if not m:
    sys.exit("в docs/salda_modular.md нет раздела «## 4.»")
body = m.group(0)
body = re.sub(r"^## 4\. Полный список покупок", "## 1. Что купить", body, flags=re.M)
body = re.sub(r"^## 5\. ", "## 2. ", body, flags=re.M)
body = body.replace("(п. 3 раздела 3)", "(docs/salda_modular.md, раздел 3)").replace("— п. 5", "— раздел 2")

n_rows = sum(1 for l in body.splitlines() if re.match(r"^\| \d+ \|", l))
intro = ("Плата Waveshare ESP32-S3-ETH-8DI-8RO + модули Modbus (0–10 В, аналоговые входы). "
         "Подключение и отличия от сборки на KC868-A8 — `docs/salda_modular.md`. "
         f"Позиций в списке: **{n_rows}**. Цен нет. «0–1» — по желанию или по результатам прозвонки.\n\n")

commit = subprocess.run(["git", "-C", str(root), "rev-parse", "--short", "HEAD"],
                        capture_output=True, text=True).stdout.strip()
today = datetime.date.today().isoformat()

with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    src = tmp / "src.md"
    html = tmp / "doc.html"
    src.write_text(intro + body, encoding="utf-8")
    fonts_css = tmp / "fonts.css"
    fonts_css.write_text("".join(
        f'@font-face {{ font-family: "{fam}"; font-weight: {w}; '
        f'src: url("data:font/ttf;base64,{base64.b64encode(path.read_bytes()).decode()}") format("truetype"); }}\n'
        for (fam, w), path in FONTS.items()), encoding="utf-8")
    subprocess.run(["pandoc", str(src), "-f", "gfm+ascii_identifiers", "-t", "html5", "-s",
                    "--metadata", "title=Список покупок: Salda RIS 400P, модульная сборка",
                    "--metadata", "pagetitle=Список покупок — модульная сборка",
                    "--metadata", f"subtitle=400 м³/ч · из docs/salda_modular.md · коммит {commit} · {today}",
                    "--css", str(fonts_css), "--css", str(tools / "wiring_pdf.css"),
                    "--css", str(tools / "bom_modular_pdf.css"),
                    "--embed-resources", "-o", str(html)], check=True)
    env = dict(os.environ, NODE_PATH=os.environ.get("NODE_PATH", "/opt/node-tools/node_modules"))
    subprocess.run(["node", str(tools / "html_to_pdf.js"), str(html), str(out), "Список покупок",
                    "Salda RIS 400P / Waveshare + модули Modbus"], check=True, env=env)

print(out)
print(f"позиций: {n_rows}")
