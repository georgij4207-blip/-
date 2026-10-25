import * as THREE from 'three';
import { OutlineEffect } from 'three/addons/effects/OutlineEffect.js';
import { zipSync } from 'fflate';
import { ANIMATIONS, clipInfo } from '../coyote/animator.js';

// Экспорт анимаций в спрайтлисты.
//
// Камера обходит койота с шагом angleStep (по умолчанию 15° — 24 направления)
// и для каждого кадра каждой анимации делает снимок на прозрачном фоне.
// Масштаб одинаковый для всех анимаций (пикселей на единицу сцены), а размер
// кадра у каждой анимации свой — по её габаритам (прыжку и падению нужно
// больше места). Кадры складываются в листы PNG: строка — направление,
// столбцы — кадры. Листы не больше 4096×4096 — предел canvas на iPad; если
// не помещается, анимация делится на несколько листов. Описание кадров —
// в JSON. Всё упаковывается в ZIP.

const MAX_SHEET = 4096;
const MARGIN = 0.18; // запас вокруг койота, единицы сцены
const SIM_STEP = 1 / 120; // шаг симуляции (пружины, переходы)

export const EXPORT_DEFAULTS = {
  heightPx: 160, // рост стоящего койота в кадре
  fps: 12,
  angleStep: 15,
  elevation: 20, // камера смотрит сверху вниз под этим углом, градусы
  shadow: false, // пятно-тень под ногами в кадре
  outlinePx: 2, // толщина контура в пикселях
  animations: ANIMATIONS.map(([id]) => id),
};

const nextTick = () => new Promise((r) => setTimeout(r, 0));

function canvasToPng(canvas) {
  return new Promise((resolve, reject) => {
    canvas.toBlob(async (blob) => {
      if (!blob) return reject(new Error('Не удалось закодировать PNG'));
      resolve(new Uint8Array(await blob.arrayBuffer()));
    }, 'image/png');
  });
}

/**
 * @param {object} ctx
 * @param {THREE.Scene} ctx.scene
 * @param {THREE.Object3D} ctx.coyote
 * @param {THREE.Object3D[]} ctx.hide — что спрятать на время съёмки (сетка, тень)
 * @param {THREE.Object3D} [ctx.shadow] — пятно-тень (показывается, если settings.shadow)
 * @param {object} [settings] — см. EXPORT_DEFAULTS
 * @param {(done: number, total: number, text: string) => void} [onProgress]
 * @returns {Promise<{ zip: Blob, json: object, files: string[] }>}
 */
export async function exportSprites({ scene, coyote, hide = [], shadow }, settings = {}, onProgress = () => {}) {
  const opt = { ...EXPORT_DEFAULTS, ...settings };
  const { skeleton, animator, face, tail, ears } = coyote.userData;
  const springs = [tail.userData.spring, ears.left.userData.spring, ears.right.userData.spring];
  const angles = [];
  for (let a = 0; a < 360; a += opt.angleStep) angles.push(a);
  const elev = THREE.MathUtils.degToRad(opt.elevation);

  // --- Сохраняем и готовим сцену ---
  const saved = {
    background: scene.background,
    fog: scene.fog,
    visible: hide.map((o) => o.visible),
    shadowVisible: shadow?.visible,
    live: face.live.enabled,
    rotationY: coyote.rotation.y,
  };
  scene.background = null;
  scene.fog = null;
  hide.forEach((o) => (o.visible = false));
  if (shadow) shadow.visible = opt.shadow;
  face.live.enabled = false; // без случайного моргания — кадры повторяемы
  coyote.rotation.y = 0;

  const skinned = [];
  coyote.traverse((o) => o.isSkinnedMesh && skinned.push(o));
  const box = new THREE.Box3();
  const frameBox = new THREE.Box3();

  function resetCoyote() {
    animator.cancel();
    skeleton.setPose({}, 0);
    springs.forEach((s) => s.reset());
    step(0);
  }

  function step(dt) {
    let left = dt;
    do {
      const h = Math.min(SIM_STEP, left);
      coyote.updateMatrixWorld(true);
      coyote.userData.update(h);
      left -= h;
    } while (left > 1e-9);
    coyote.updateMatrixWorld(true);
    if (shadow?.visible) {
      // Тень меньше и бледнее, когда койот в воздухе (как на стенде)
      const air = Math.max(0, coyote.position.y);
      shadow.scale.setScalar(1 / (1 + air * 0.35));
      shadow.material.opacity = 1 / (1 + air * 0.6);
    }
  }

  function measure(target) {
    for (const m of skinned) m.boundingBox = null; // пересчитать с учётом костей
    frameBox.setFromObject(coyote);
    target.union(frameBox);
  }

  // Время кадров анимации; для циклов — ровно один период после разгона
  function framePlan(name) {
    const { duration, loop } = clipInfo(name);
    if (loop) {
      const count = Math.max(2, Math.round(duration * opt.fps));
      return { loop, count, frameDuration: duration / count, warmup: duration * 2 };
    }
    const count = Math.floor(duration * opt.fps + 1e-6) + 1;
    return { loop, count, frameDuration: 1 / opt.fps, warmup: 0 };
  }

  // Прогон анимации: callback(frameIndex) на каждом кадре
  async function run(name, plan, onFrame) {
    resetCoyote();
    animator.play(name);
    step(plan.warmup);
    for (let i = 0; i < plan.count; i++) {
      if (i > 0) step(plan.frameDuration);
      await onFrame(i);
    }
  }

  // --- Масштаб: рост стоящего койота → heightPx ---
  resetCoyote();
  box.makeEmpty();
  measure(box);
  const ppu = opt.heightPx / (box.max.y - box.min.y);

  // Рендерер для кадров: прозрачный фон, свой контур под размер кадра
  const renderer = new THREE.WebGLRenderer({ alpha: true, antialias: true, preserveDrawingBuffer: true });
  renderer.setPixelRatio(1);
  renderer.setClearColor(0x000000, 0);
  const camera = new THREE.OrthographicCamera();

  const plans = opt.animations.map((name) => ({ name, plan: framePlan(name) }));
  const totalShots = plans.reduce((n, p) => n + p.plan.count * angles.length, 0);
  let shots = 0;

  const files = {};
  const json = {
    meta: {
      app: 'coyote-stand',
      version: 1,
      pixelsPerUnit: +ppu.toFixed(4),
      fps: opt.fps,
      angleStep: opt.angleStep,
      elevation: opt.elevation,
      directions: angles,
      directionsNote:
        'Направление — азимут камеры вокруг койота: 0 — анфас, 90 — его левый бок, 180 — спина, 270 — правый бок.',
      anchorNote: 'anchor — точка на полу под койотом (начало координат персонажа) в пикселях от левого верхнего угла кадра.',
    },
    animations: {},
  };

  try {
    for (const { name, plan } of plans) {
      const label = ANIMATIONS.find(([id]) => id === name)[1];

      // 1. Габариты анимации по всем кадрам
      onProgress(shots, totalShots, `${label}: замер`);
      box.makeEmpty();
      await run(name, plan, () => measure(box));
      const R =
        Math.max(Math.hypot(box.min.x, box.min.z), Math.hypot(box.max.x, box.max.z),
          Math.hypot(box.min.x, box.max.z), Math.hypot(box.max.x, box.min.z)) + MARGIN;
      const top = box.max.y * Math.cos(elev) + R * Math.sin(elev) + MARGIN;
      const bottom = Math.min(0, box.min.y) * Math.cos(elev) - R * Math.sin(elev) - MARGIN;
      const fw = Math.ceil(2 * R * ppu);
      const fh = Math.ceil((top - bottom) * ppu);

      camera.left = -fw / ppu / 2;
      camera.right = fw / ppu / 2;
      camera.top = top;
      camera.bottom = top - fh / ppu;
      camera.near = 0.1;
      camera.far = 200;
      camera.updateProjectionMatrix();
      renderer.setSize(fw, fh, false);
      // Контур OutlineEffect задан в долях высоты кадра: держим ~outlinePx пикселей
      const effect = new OutlineEffect(renderer, { defaultThickness: (2 * opt.outlinePx) / fh });

      // 2. Раскладка по листам
      const cols = Math.min(plan.count, Math.floor(MAX_SHEET / fw));
      const rowsPerDir = Math.ceil(plan.count / cols);
      const dirsPerSheet = Math.max(1, Math.floor(Math.floor(MAX_SHEET / fh) / rowsPerDir));
      const sheetCount = Math.ceil(angles.length / dirsPerSheet);
      const sheets = Array.from({ length: sheetCount }, (_, s) => {
        const dirs = Math.min(dirsPerSheet, angles.length - s * dirsPerSheet);
        const canvas = document.createElement('canvas');
        canvas.width = cols * fw;
        canvas.height = dirs * rowsPerDir * fh;
        return { canvas, ctx: canvas.getContext('2d'), file: sheetCount > 1 ? `${name}_${s + 1}.png` : `${name}.png` };
      });

      const directions = Object.fromEntries(angles.map((a) => [a, []]));
      const dist = 50;

      // 3. Съёмка: на каждом кадре камера обходит койота
      await run(name, plan, async (frame) => {
        for (let d = 0; d < angles.length; d++) {
          const az = THREE.MathUtils.degToRad(angles[d]);
          camera.position.set(
            Math.sin(az) * Math.cos(elev) * dist,
            Math.sin(elev) * dist,
            Math.cos(az) * Math.cos(elev) * dist
          );
          camera.lookAt(0, 0, 0);
          // OutlineEffect сам не очищает буфер (его autoClear не задан) —
          // при прозрачном фоне кадры наслаивались бы друг на друга
          renderer.clear();
          effect.render(scene, camera);

          const sheetIndex = Math.floor(d / dirsPerSheet);
          const dirRow = (d % dirsPerSheet) * rowsPerDir + Math.floor(frame / cols);
          const x = (frame % cols) * fw;
          const y = dirRow * fh;
          sheets[sheetIndex].ctx.drawImage(renderer.domElement, x, y);
          directions[angles[d]].push({ sheet: sheetIndex, x, y });

          shots++;
          if (shots % 8 === 0) {
            onProgress(shots, totalShots, `${label}: кадр ${frame + 1}/${plan.count}`);
            await nextTick();
          }
        }
      });

      // 4. Кодирование листов
      onProgress(shots, totalShots, `${label}: сохраняю PNG`);
      for (const sheet of sheets) files[sheet.file] = await canvasToPng(sheet.canvas);

      json.animations[name] = {
        label,
        loop: plan.loop,
        frameCount: plan.count,
        frameDuration: +plan.frameDuration.toFixed(5),
        frameWidth: fw,
        frameHeight: fh,
        anchor: { x: +(fw / 2).toFixed(2), y: +(top * ppu).toFixed(2) },
        sheets: sheets.map((s) => ({ file: s.file, width: s.canvas.width, height: s.canvas.height })),
        directions,
      };
    }

    files['coyote_sprites.json'] = new TextEncoder().encode(JSON.stringify(json, null, 2));
    onProgress(totalShots, totalShots, 'Упаковываю ZIP');
    await nextTick();
    // PNG уже сжаты — кладём без повторного сжатия, JSON сжимаем
    const zipped = zipSync(
      Object.fromEntries(Object.entries(files).map(([n, data]) => [n, [data, { level: n.endsWith('.json') ? 6 : 0 }]]))
    );
    return { zip: new Blob([zipped], { type: 'application/zip' }), json, files: Object.keys(files) };
  } finally {
    renderer.dispose();
    renderer.forceContextLoss();
    resetCoyote();
    scene.background = saved.background;
    scene.fog = saved.fog;
    hide.forEach((o, i) => (o.visible = saved.visible[i]));
    if (shadow) shadow.visible = saved.shadowVisible;
    face.live.enabled = saved.live;
    coyote.rotation.y = saved.rotationY;
  }
}
