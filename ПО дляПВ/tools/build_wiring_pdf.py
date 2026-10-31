# Схема подключения щитка одним PDF: docs/panel_wiring.md + docs/ssr_driver.md → docs/схема_щитка.pdf
# Путь: pandoc (md → промежуточный HTML) → Chromium/Playwright (HTML → PDF).
# Шрифты встраиваются явно: DejaVu Sans из firmware/fonts/, DejaVu Sans Mono (схемы) — системный.
# После сборки PDF проверяется: tools/check_wiring_pdf.py (дубли букв, разрывы, все клеммы XT).
#   python3 tools/build_wiring_pdf.py
import base64, datetime, os, pathlib, shutil, subprocess, sys, tempfile

root = pathlib.Path(__file__).resolve().parent.parent
tools = root / "tools"
out = root / "docs" / "схема_щитка.pdf"
html_out = root / "docs" / "схема_щитка.html"   # промежуточный HTML (для печати из браузера)

for tool in ("pandoc", "node"):
    if not shutil.which(tool):
        sys.exit(f"не найден {tool}: установите его и повторите")

FONTS = {
    ("DejaVu Sans", "normal"): root / "firmware/fonts/DejaVuSans.ttf",
    ("DejaVu Sans", "bold"): root / "firmware/fonts/DejaVuSans-Bold.ttf",
    ("DejaVu Sans Mono", "normal"): pathlib.Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"),
    ("DejaVu Sans Mono", "bold"): pathlib.Path("/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"),
}
for f in FONTS.values():
    if not f.exists():
        sys.exit(f"нет шрифта {f}")

commit = subprocess.run(["git", "-C", str(root), "rev-parse", "--short", "HEAD"],
                        capture_output=True, text=True).stdout.strip()

panel = (root / "docs/panel_wiring.md").read_text(encoding="utf-8")
ssr = (root / "docs/ssr_driver.md").read_text(encoding="utf-8")
md = panel + '\n\n<div class="page-break"></div>\n\n' + ssr

with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    src = tmp / "src.md"
    src.write_text(md, encoding="utf-8")
    fonts_css = tmp / "fonts.css"
    fonts_css.write_text("".join(
        f'@font-face {{ font-family: "{fam}"; font-weight: {w}; '
        f'src: url("data:font/ttf;base64,{base64.b64encode(path.read_bytes()).decode()}") format("truetype"); }}\n'
        for (fam, w), path in FONTS.items()), encoding="utf-8")
    subprocess.run(["pandoc", str(src), "-f", "gfm+ascii_identifiers", "-t", "html5", "-s",
                    "--metadata", "title=Схема подключения щитка",
                    "--metadata", "pagetitle=Схема подключения щитка",
                    "--metadata", f"subtitle=Salda RIS 400P на KinCony KC868-A8 · из docs/panel_wiring.md и "
                                  f"docs/ssr_driver.md · коммит {commit} · {datetime.date.today().isoformat()}",
                    "--css", str(fonts_css), "--css", str(tools / "wiring_pdf.css"), "--embed-resources",
                    "--toc", "--toc-depth=3",
                    "-o", str(html_out)], check=True)
    env = dict(os.environ, NODE_PATH=os.environ.get("NODE_PATH", "/opt/node-tools/node_modules"))
    subprocess.run(["node", str(tools / "html_to_pdf.js"), str(html_out), str(out)], check=True, env=env)

print(out)
r = subprocess.run([sys.executable, str(tools / "check_wiring_pdf.py")])
sys.exit(r.returncode)
