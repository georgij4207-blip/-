# Схема подключения щитка одним PDF: docs/panel_wiring.md + docs/ssr_driver.md +
# раздел «Подключение» из docs/display_co2.md → docs/схема_щитка.pdf
# Нужны: pandoc (md → html) и Node.js с Playwright/Chromium (html → pdf).
#   python3 tools/build_wiring_pdf.py
import datetime, os, pathlib, shutil, subprocess, sys, tempfile

root = pathlib.Path(__file__).resolve().parent.parent
tools = root / "tools"
out = root / "docs" / "схема_щитка.pdf"

for tool in ("pandoc", "node"):
    if not shutil.which(tool):
        sys.exit(f"не найден {tool}: установите его и повторите")

commit = subprocess.run(["git", "-C", str(root), "rev-parse", "--short", "HEAD"],
                        capture_output=True, text=True).stdout.strip()

def section(text, start, end=None):
    a = text.index(start)
    b = text.index(end, a + len(start)) if end else len(text)
    return text[a:b]

panel = (root / "docs/panel_wiring.md").read_text(encoding="utf-8")
ssr = (root / "docs/ssr_driver.md").read_text(encoding="utf-8")
oled = section((root / "docs/display_co2.md").read_text(encoding="utf-8"), "## Подключение", "\n## Экран")
oled = oled.replace("## Подключение", "# Экран OLED и датчик CO2: подключение", 1)

brk = '\n\n<div class="page-break"></div>\n\n'
md = brk.join([panel, ssr, oled])

with tempfile.TemporaryDirectory() as tmp:
    src = pathlib.Path(tmp) / "src.md"
    html = pathlib.Path(tmp) / "doc.html"
    src.write_text(md, encoding="utf-8")
    subprocess.run(["pandoc", str(src), "-f", "gfm+ascii_identifiers", "-t", "html5", "-s",
                    "--metadata", "title=Схема подключения щитка",
                    "--metadata", "pagetitle=Схема подключения щитка",
                    "--css", str(tools / "wiring_pdf.css"), "--embed-resources",
                    "--toc", "--toc-depth=2",
                    "--metadata", f"subtitle=Salda RIS 400P на KinCony KC868-A8 · коммит {commit} · "
                                  f"{datetime.date.today().isoformat()}",
                    "-o", str(html)], check=True)
    env = dict(os.environ, NODE_PATH=os.environ.get("NODE_PATH", "/opt/node-tools/node_modules"))
    subprocess.run(["node", str(tools / "html_to_pdf.js"), str(html), str(out)], check=True, env=env)

print(out)
