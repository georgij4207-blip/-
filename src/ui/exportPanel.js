import { ANIMATIONS } from '../coyote/animator.js';
import { exportSprites } from '../export/spriteExporter.js';

// Панель «Экспорт спрайтов»: настройки, запуск, прогресс и скачивание ZIP.
export function setupExportPanel({ scene, coyote, hide, shadow }) {
  const $ = (id) => document.getElementById(id);
  const side = document.querySelector('.side');
  const animBox = $('exp-anims');
  const start = $('exp-start');
  const progress = $('exp-progress');
  const status = $('exp-status');
  const download = $('exp-download');

  const state = { busy: false, zip: null, zipUrl: null };
  const selected = new Set(ANIMATIONS.map(([id]) => id));

  for (const [id, label] of ANIMATIONS) {
    const chip = document.createElement('button');
    chip.type = 'button';
    chip.textContent = label;
    chip.classList.add('active');
    chip.addEventListener('click', () => {
      if (selected.has(id)) selected.delete(id);
      else selected.add(id);
      chip.classList.toggle('active', selected.has(id));
      start.disabled = selected.size === 0;
    });
    animBox.append(chip);
  }

  start.addEventListener('click', async () => {
    if (state.busy) return;
    state.busy = true;
    side.classList.add('busy');
    start.disabled = true;
    download.disabled = true;
    progress.hidden = false;
    progress.value = 0;
    if (state.zipUrl) URL.revokeObjectURL(state.zipUrl);
    state.zipUrl = null;

    const settings = {
      heightPx: Number($('exp-height').value),
      fps: Number($('exp-fps').value),
      angleStep: Number($('exp-step').value),
      shadow: $('exp-shadow').checked,
      animations: ANIMATIONS.map(([id]) => id).filter((id) => selected.has(id)),
    };
    const t0 = performance.now();
    try {
      const { zip, json, files } = await exportSprites({ scene, coyote, hide, shadow }, settings, (done, total, text) => {
        progress.value = total ? done / total : 0;
        status.textContent = text;
      });
      state.zip = zip;
      state.zipUrl = URL.createObjectURL(zip);
      const frames = Object.values(json.animations).reduce((n, a) => n + a.frameCount, 0);
      const dirs = json.meta.directions.length;
      status.textContent =
        `Готово за ${((performance.now() - t0) / 1000).toFixed(1)} с: ` +
        `${frames} кадров × ${dirs} направлений = ${frames * dirs} спрайтов\n` +
        `${files.length - 1} листов PNG + coyote_sprites.json, ${(zip.size / 1048576).toFixed(1)} МБ`;
      download.disabled = false;
      download.scrollIntoView({ block: 'nearest', behavior: 'smooth' }); // кнопка может быть ниже края панели
    } catch (err) {
      console.error(err);
      status.textContent = `Ошибка экспорта: ${err.message}`;
    } finally {
      state.busy = false;
      side.classList.remove('busy');
      start.disabled = selected.size === 0;
      progress.hidden = true;
    }
  });

  // Внутри Artifact на claude.ai страница не может скачивать сама —
  // файл отдаётся через возможность downloads (зритель подтверждает
  // сохранение). При локальном запуске — обычная ссылка.
  download.addEventListener('click', async () => {
    if (!state.zip) return;
    const downloads = window.claude?.use ? await window.claude.use('downloads') : null;
    if (downloads) {
      try {
        await downloads.save({ filename: 'coyote_sprites.zip', data: state.zip });
        status.textContent += '\nАрхив сохранён.';
      } catch (err) {
        if (err?.code === 'declined') status.textContent += '\nСохранение отменено.';
        else if (err?.code === 'rate_limited') status.textContent += '\nОкно сохранения уже открыто — ответьте в нём.';
        else status.textContent += `\nНе удалось сохранить архив (${err?.code ?? err}).`;
      }
      return;
    }
    const a = document.createElement('a');
    a.href = state.zipUrl;
    a.download = 'coyote_sprites.zip';
    document.body.append(a);
    a.click();
    a.remove();
  });

  return state;
}
