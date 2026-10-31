// HTML → PDF через Chromium (Playwright). Вызывается из tools/build_wiring_pdf.py.
//   node html_to_pdf.js in.html out.pdf
// Каждая ASCII-схема (<pre>) уменьшается до ширины листа, но не мельче MIN_PT;
// если схема всё равно не влезает или таблица шире листа — ошибка, а не тихая обрезка.
const { chromium } = require('playwright');
const path = require('path');

const MIN_PT = 6.5;

(async () => {
  const [inp, out] = process.argv.slice(2);
  const browser = await chromium.launch();
  const page = await browser.newPage();
  await page.goto('file://' + path.resolve(inp));
  await page.emulateMedia({ media: 'print' });
  // ширина области печати: A4 альбомный 297 мм − поля 12 + 12 мм
  await page.addStyleTag({ content: 'body { width: 273mm; }' });
  await page.evaluate(() => document.fonts.ready);

  const problems = await page.evaluate((minPt) => {
    const bad = [];
    for (const pre of document.querySelectorAll('pre')) {
      let pt = parseFloat(getComputedStyle(pre).fontSize) * 0.75;
      while (pre.scrollWidth > pre.clientWidth + 0.5 && pt > minPt) {
        pt = Math.max(minPt, pt - 0.25);
        pre.style.fontSize = pt + 'pt';
      }
      if (pre.scrollWidth > pre.clientWidth + 0.5)
        bad.push('схема не влезает по ширине: ' + pre.textContent.slice(0, 60));
    }
    const body = document.body.getBoundingClientRect().width;
    for (const t of document.querySelectorAll('table'))
      if (t.getBoundingClientRect().width > body + 0.5)
        bad.push('таблица шире листа: ' + t.textContent.trim().slice(0, 60));
    return bad;
  }, MIN_PT);
  if (problems.length) {
    console.error(problems.join('\n'));
    process.exit(1);
  }

  await page.pdf({
    path: out,
    format: 'A4',
    landscape: true,
    preferCSSPageSize: true,
    printBackground: true,
    displayHeaderFooter: true,
    headerTemplate: '<span></span>',
    footerTemplate: '<div style="width:100%;font-size:7pt;color:#666;padding:0 12mm;display:flex;justify-content:space-between;font-family:DejaVu Sans,sans-serif">' +
      '<span>Схема подключения щитка — Salda RIS 400P / KC868-A8</span>' +
      '<span>стр. <span class="pageNumber"></span> из <span class="totalPages"></span></span></div>',
  });
  await browser.close();
})().catch((e) => { console.error(e); process.exit(1); });
