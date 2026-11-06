# Схемы плат по отдельным листам: docs/boards_assembly.md → docs/схемы_плат.pdf (A4 альбомный).
# Нужны: pandoc и Node.js с Playwright/Chromium (tools/html_to_pdf.js). Шрифты DejaVu встроены.
#   python3 tools/build_boards_pdf.py
import base64, datetime, os, pathlib, shutil, subprocess, sys, tempfile

root = pathlib.Path(__file__).resolve().parent.parent
tools = root / "tools"
src_md = root / "docs" / "boards_assembly.md"
out = root / "docs" / "схемы_плат.pdf"

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
md = src_md.read_text(encoding="utf-8")
md = md.split("\n", 1)[1]  # заголовок — в title
with tempfile.TemporaryDirectory() as tmp:
    tmp = pathlib.Path(tmp)
    src = tmp / "src.md"; html = tmp / "doc.html"
    src.write_text(md, encoding="utf-8")
    fonts_css = tmp / "fonts.css"
    fonts_css.write_text("".join(
        f'@font-face {{ font-family: "{fam}"; font-weight: {w}; '
        f'src: url("data:font/ttf;base64,{base64.b64encode(p.read_bytes()).decode()}") format("truetype"); }}\n'
        for (fam, w), p in FONTS.items()), encoding="utf-8")
    subprocess.run(["pandoc", str(src), "-f", "gfm+ascii_identifiers", "-t", "html5", "-s",
                    "--metadata", "title=Схемы плат: что, куда и в каком порядке",
                    "--metadata", "pagetitle=Схемы плат",
                    "--metadata", f"subtitle=Salda RIS 400P на KC868-A8 · из docs/boards_assembly.md · "
                                  f"коммит {commit} · {datetime.date.today().isoformat()}",
                    "--css", str(fonts_css), "--css", str(tools / "wiring_pdf.css"), "--css", str(tools / "boards_pdf.css"),
                    "--embed-resources", "-o", str(html)], check=True)
    env = dict(os.environ, NODE_PATH=os.environ.get("NODE_PATH", "/opt/node-tools/node_modules"))
    subprocess.run(["node", str(tools / "html_to_pdf.js"), str(html), str(out), "Схемы плат"],
                   check=True, env=env)
print(out)
