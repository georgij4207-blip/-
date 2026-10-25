import * as THREE from 'three';
import { toonMaterial, twoToneMaterial, NO_OUTLINE } from '../toon.js';
import { SNOUT_PROFILE, SNOUT_SCALE, snoutRadius } from './head.js';

// Мимика: веки, зрачки, рот (нижняя челюсть, уголки), а также эффекты
// «после взрыва» — копоть и звёздочки над головой.
//
// Значения мимики живут в face.state (их пишет единый скелет), а
// face.update(dt) каждый кадр переводит их в повороты и формы и добавляет
// «живой» слой: моргание и мелкие движения взгляда.

const MOUTH_COLOR = 0x6e2324;
const TONGUE_COLOR = 0xe0787a;
const LINE_COLOR = 0x3a2c24;
const SOOT_COLOR = new THREE.Color(0x2b2420);
const STAR_COLOR = 0xffd23f;

// Шарнир челюсти в осях морды (z — вдоль морды, y — вверх)
const HINGE = new THREE.Vector3(0, -0.2, 0.02);
const JAW_LENGTH = 0.86; // доля длины морды
const JAW_CENTER_Y = -0.27; // ось нижней челюсти у основания, в осях морды
const JAW_TILT = -0.2; // челюсть идёт вверх к кончику вслед за мордой

const LID_OPEN = -1.75; // поворот века: открыто (убрано назад за глаз)
const LID_CLOSED = 1.5; // закрыто (накрывает белок спереди)

// --- Рот ---

function createJaw(colors) {
  // Тот же профиль, что у морды, но короче и тоньше; верхняя половина
  // в покое спрятана внутри морды, нижняя выступает подбородком
  const profile = SNOUT_PROFILE.map(([r, y]) => new THREE.Vector2(r * 0.8, y * JAW_LENGTH));
  const geometry = new THREE.LatheGeometry(profile, 40);
  geometry.rotateX(Math.PI / 2);
  geometry.scale(0.62, 0.42, SNOUT_SCALE.z); // уже морды — в покое полость рта спрятана
  geometry.rotateX(JAW_TILT);
  const base = new THREE.Vector3(0, JAW_CENTER_Y - HINGE.y, 0.04 - HINGE.z); // основание челюсти от шарнира
  geometry.translate(base.x, base.y, base.z);
  geometry.computeVertexNormals();

  // Сверху — тёмная полость рта, снизу — светлый подбородок; граница
  // наклонена вместе с челюстью
  const up = new THREE.Vector3(0, 1, 0).applyAxisAngle(new THREE.Vector3(1, 0, 0), JAW_TILT);
  const jaw = new THREE.Mesh(
    geometry,
    twoToneMaterial({ top: MOUTH_COLOR, bottom: colors.cream, normal: up, offset: base.dot(up) + 0.06 })
  );
  jaw.name = 'jaw';

  const tongue = new THREE.Mesh(new THREE.SphereGeometry(1, 24, 12), toonMaterial(TONGUE_COLOR));
  tongue.scale.set(0.13, 0.05, 0.3);
  tongue.rotation.x = JAW_TILT;
  tongue.position.set(0, 0.05, 0.4).applyAxisAngle(new THREE.Vector3(1, 0, 0), JAW_TILT).add(base);
  tongue.name = 'tongue';

  return { jaw, tongue };
}

// Точка на боковой поверхности морды на высоте y и расстоянии z
function snoutSidePoint(side, y, z, out = new THREE.Vector3()) {
  const r = snoutRadius(z);
  const ry = r * SNOUT_SCALE.y;
  const k = Math.sqrt(Math.max(0, 1 - (y / ry) ** 2));
  return out.set(side * (r * SNOUT_SCALE.x * k + 0.012), y, z);
}

// Уголок рта: короткая линия вдоль боковой стороны морды; smile > 0
// загибает её вверх (улыбка), < 0 — вниз (недовольство)
function cornerGeometry(side, smile) {
  const points = [
    snoutSidePoint(side, -0.23, 0.55),
    snoutSidePoint(side, -0.22 + smile * 0.04, 0.38),
    snoutSidePoint(side, -0.2 + smile * 0.12, 0.22),
    snoutSidePoint(side, -0.17 + smile * 0.22, 0.1),
  ];
  return new THREE.TubeGeometry(new THREE.CatmullRomCurve3(points), 20, 0.026, 8, false);
}

// --- Глаза ---

function createLid(eye, color) {
  const white = eye.getObjectByName('white');
  const lidScale = new THREE.Group();
  lidScale.scale.copy(white.scale).multiplyScalar(1.07);
  const pivot = new THREE.Group();
  pivot.name = 'lid';
  // Верхняя полусфера: поворотом вперёд накрывает белок
  const cap = new THREE.Mesh(
    new THREE.SphereGeometry(1, 32, 16, 0, Math.PI * 2, 0, Math.PI / 2),
    toonMaterial(color)
  );
  cap.material.side = THREE.DoubleSide;
  pivot.add(cap);
  lidScale.add(pivot);
  eye.add(lidScale);
  return pivot;
}

// --- Звёздочки над головой ---

function createStars() {
  const shape = new THREE.Shape();
  for (let i = 0; i < 10; i++) {
    const a = (i / 10) * Math.PI * 2 + Math.PI / 2;
    const r = i % 2 === 0 ? 1 : 0.45;
    if (i === 0) shape.moveTo(Math.cos(a) * r, Math.sin(a) * r);
    else shape.lineTo(Math.cos(a) * r, Math.sin(a) * r);
  }
  const geometry = new THREE.ExtrudeGeometry(shape, { depth: 0.35, bevelEnabled: false });
  geometry.center();
  geometry.scale(0.11, 0.11, 0.11);

  const ring = new THREE.Group();
  ring.name = 'stars';
  ring.position.y = 0.95;
  const material = toonMaterial(STAR_COLOR);
  for (let i = 0; i < 5; i++) {
    const star = new THREE.Mesh(geometry, material);
    const a = (i / 5) * Math.PI * 2;
    star.position.set(Math.cos(a) * 0.62, 0, Math.sin(a) * 0.62);
    ring.add(star);
  }
  ring.visible = false;
  return ring;
}

// --- Сборка ---

export const DEFAULT_FACE = {
  lookX: 0, // градусы: + вверх
  lookY: 0, // + влево (от койота)
  cross: 0, // + глаза к носу
  pupil: 0, // размер зрачка, %: + больше
  lidL: 0, // закрытие века, %: 0 — открыто, 100 — закрыто
  lidR: 0,
  jaw: 0, // открытие рта, градусы
  smileL: 0, // уголок рта, %: + вверх, − вниз
  smileR: 0,
  soot: 0, // копоть, %
  stars: 0, // звёздочки, %
};

export function createFace(head, colors) {
  const snout = head.getObjectByName('snout');
  const eyes = [head.getObjectByName('eyeL'), head.getObjectByName('eyeR')];

  // Рот
  const jawPivot = new THREE.Group();
  jawPivot.name = 'jawPivot';
  jawPivot.position.copy(HINGE);
  snout.add(jawPivot);
  const { jaw, tongue } = createJaw(colors);
  jawPivot.add(jaw, tongue);

  // Полость рта поворачивается на половину угла челюсти — заполняет щель
  const mouthPivot = new THREE.Group();
  mouthPivot.position.copy(HINGE);
  snout.add(mouthPivot);
  const cavity = new THREE.Mesh(new THREE.SphereGeometry(1, 24, 12), toonMaterial(MOUTH_COLOR));
  cavity.scale.set(0.17, 0.07, 0.4);
  cavity.rotation.x = JAW_TILT;
  cavity.position.set(0, 0.03, 0.42);
  cavity.material.userData.outlineParameters = NO_OUTLINE;
  mouthPivot.add(cavity);

  const lineMaterial = toonMaterial(LINE_COLOR);
  lineMaterial.userData.outlineParameters = NO_OUTLINE;
  const corners = [1, -1].map((side) => {
    const corner = new THREE.Mesh(cornerGeometry(side, 0), lineMaterial);
    corner.name = side > 0 ? 'mouthCornerL' : 'mouthCornerR';
    corner.userData = { side, smile: 0 };
    snout.add(corner);
    return corner;
  });

  // Глаза
  const lids = eyes.map((eye) => createLid(eye, colors.fur));
  const looks = eyes.map((eye) => eye.getObjectByName('look'));
  const pupils = looks.map((look) => look.getObjectByName('pupil'));
  const pupilScale = pupils[0].scale.clone();

  // Эффекты
  const stars = createStars();
  head.add(stars);

  const state = { ...DEFAULT_FACE };
  const live = {
    enabled: true,
    blinkIn: 1.5, // секунд до следующего моргания
    blinkT: -1, // фаза текущего моргания (−1 — не моргает)
    saccadeIn: 1,
    saccade: new THREE.Vector2(),
    saccadeTarget: new THREE.Vector2(),
  };
  let sootTargets = [];
  let sootApplied = -1;
  let time = 0;

  function writeCorners() {
    for (const corner of corners) {
      const s = corner.userData.side > 0 ? state.smileL : state.smileR;
      const smile = Math.round(s) / 100;
      if (smile === corner.userData.smile) continue;
      corner.geometry.dispose();
      corner.geometry = cornerGeometry(corner.userData.side, smile);
      corner.userData.smile = smile;
    }
  }

  function writeSoot() {
    if (state.soot === sootApplied) return;
    sootApplied = state.soot;
    const k = (state.soot / 100) * 0.85;
    for (const { color, base } of sootTargets) color.copy(base).lerp(SOOT_COLOR, k);
  }

  function update(dt) {
    time += dt;

    // Живой слой: моргание и мелкие перескоки взгляда
    let blink = 0;
    if (live.enabled) {
      live.blinkIn -= dt;
      if (live.blinkIn <= 0 && live.blinkT < 0) live.blinkT = 0;
      if (live.blinkT >= 0) {
        live.blinkT += dt / 0.18;
        blink = Math.sin(Math.min(live.blinkT, 1) * Math.PI); // закрыть и открыть
        if (live.blinkT >= 1) {
          live.blinkT = -1;
          live.blinkIn = 1.8 + Math.random() * 3.5;
        }
      }
      live.saccadeIn -= dt;
      if (live.saccadeIn <= 0) {
        live.saccadeIn = 0.8 + Math.random() * 2.2;
        live.saccadeTarget.set((Math.random() - 0.5) * 10, (Math.random() - 0.5) * 18);
      }
    } else {
      live.saccadeTarget.set(0, 0);
    }
    live.saccade.lerp(live.saccadeTarget, Math.min(1, dt * 18)); // быстрый, как у глаз, перескок

    // Веки
    lids.forEach((lid, i) => {
      const base = (i === 0 ? state.lidL : state.lidR) / 100;
      const close = Math.max(base, blink);
      lid.rotation.x = LID_OPEN + (LID_CLOSED - LID_OPEN) * close;
    });

    // Взгляд и зрачки
    looks.forEach((look, i) => {
      const side = i === 0 ? 1 : -1;
      look.rotation.x = -THREE.MathUtils.degToRad(state.lookX + live.saccade.x);
      look.rotation.y = THREE.MathUtils.degToRad(state.lookY + live.saccade.y - side * state.cross);
    });
    const ps = 1 + state.pupil / 100;
    pupils.forEach((p) => p.scale.set(pupilScale.x * ps, pupilScale.y * ps, pupilScale.z));

    // Рот
    const jawAngle = THREE.MathUtils.degToRad(state.jaw);
    jawPivot.rotation.x = jawAngle;
    mouthPivot.rotation.x = jawAngle * 0.5;
    writeCorners();

    // Эффекты
    writeSoot();
    stars.visible = state.stars > 1;
    if (stars.visible) {
      const s = state.stars / 100;
      stars.scale.setScalar(s);
      stars.rotation.y = time * 2.4;
      stars.position.y = 0.95 + Math.sin(time * 3) * 0.05;
      stars.children.forEach((star, i) => (star.rotation.y = -time * 2.4 + i));
    }
  }

  // Копоть ложится на все материалы персонажа, кроме белков, бликов и звёзд
  function setSootRoot(root) {
    const skip = new Set([STAR_COLOR]);
    const seen = new Set();
    sootTargets = [];
    root.traverse((o) => {
      if (!o.isMesh || seen.has(o.material) || o.name === 'white' || o.parent === stars) return;
      const m = o.material;
      seen.add(m);
      if (m.isMeshBasicMaterial || skip.has(m.color.getHex())) return;
      sootTargets.push({ color: m.color, base: m.color.clone() });
      const tt = m.userData.twoTone;
      if (tt) sootTargets.push({ color: tt.uBottomColor.value, base: tt.uBottomColor.value.clone() });
    });
    sootApplied = -1;
  }

  update(0);
  return { state, live, update, setSootRoot };
}
