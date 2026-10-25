// Собирает стенд в один HTML-файл для публикации как Artifact на claude.ai:
// стили и весь JS встраиваются в страницу, без <html>/<head>/<body> —
// их добавляет сам Artifact. Результат: dist/coyote-stand.html
import { readFileSync, writeFileSync } from 'node:fs';
import { execSync } from 'node:child_process';

execSync('npx vite build', { stdio: 'inherit' });

const html = readFileSync('dist/index.html', 'utf8');
const cssFile = html.match(/href="\/(assets\/[^"]+\.css)"/)[1];
const jsFile = html.match(/src="\/(assets\/[^"]+\.js)"/)[1];
const css = readFileSync(`dist/${cssFile}`, 'utf8');
const js = readFileSync(`dist/${jsFile}`, 'utf8').replace(/<\/script/gi, '<\\/script');

const title = html.match(/<title>[\s\S]*?<\/title>/)[0];
const body = html
  .match(/<body>([\s\S]*)<\/body>/)[1]
  .replace(/<script[^>]*src="[^"]*"[^>]*><\/script>/, '')
  .trim();

const page = `${title}
<meta name="theme-color" content="#eef1f5">
<style>
${css}
</style>
${body}
<script type="module">
${js}
</script>
`;
writeFileSync('dist/coyote-stand.html', page);
console.log(`dist/coyote-stand.html — ${(page.length / 1024).toFixed(0)} КБ`);
