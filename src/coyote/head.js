import * as THREE from 'three';
import { toonMaterial, twoToneMaterial, flatMaterial } from '../toon.js';
import { createEars } from './ears.js';

// Голова койота в духе анимации 50-х: круглый череп, длинная заострённая
// морда с носом-пуговицей, большие глаза «парой» над переносицей и
// отдельные брови. Начало координат — центр черепа, морда смотрит в +Z.

export const COYOTE_COLORS = {
  fur: 0xa98a6d, // рыже-серая шерсть
  furDark: 0x786454, // хохолок и бакенбарды
  cream: 0xf1e3c6, // светлая морда
  brow: 0x4a3a2f,
  tailTip: 0x4e4036, // тёмный кончик хвоста
  earInner: 0xf0a8a0, // розоватая внутренняя часть уха
  nose: 0x1d1f24,
  eyeWhite: 0xffffff,
  pupil: 0x1d1f24,
};

const C = COYOTE_COLORS;

// Морда: тело вращения, сужающееся к кончику. Профиль (радиус, длина)
// задаётся вдоль Y, затем геометрия поворачивается вдоль +Z. Профиль и
// масштаб экспортируются — по ним подгоняются челюсть и уголки рта.
export const SNOUT_PROFILE = [
  [0.0, -0.2],
  [0.3, -0.17],
  [0.42, 0.0],
  [0.41, 0.2],
  [0.36, 0.45],
  [0.28, 0.72],
  [0.2, 0.95],
  [0.13, 1.1],
  [0.06, 1.18],
  [0.0, 1.2],
];
export const SNOUT_SCALE = new THREE.Vector3(0.88, 0.82, 0.92); // чуть уже и ниже, чем круглая

// Радиус морды (до масштаба по X/Y) на расстоянии z от основания, в осях морды
export function snoutRadius(z) {
  const y = z / SNOUT_SCALE.z;
  for (let i = 1; i < SNOUT_PROFILE.length; i++) {
    const [r0, y0] = SNOUT_PROFILE[i - 1];
    const [r1, y1] = SNOUT_PROFILE[i];
    if (y <= y1) return r0 + ((r1 - r0) * (y - y0)) / (y1 - y0);
  }
  return 0;
}

function createSnoutGeometry() {
  const profile = SNOUT_PROFILE.map(([r, y]) => new THREE.Vector2(r, y));
  const geometry = new THREE.LatheGeometry(profile, 48);
  geometry.rotateX(Math.PI / 2); // ось вращения Y → Z
  geometry.scale(SNOUT_SCALE.x, SNOUT_SCALE.y, SNOUT_SCALE.z);
  geometry.computeVertexNormals();
  return geometry;
}

// Бровь: капсула, изогнутая дугой и приплюснутая спереди назад.
function createBrowGeometry() {
  const geometry = new THREE.CapsuleGeometry(0.055, 0.3, 6, 16);
  geometry.rotateZ(Math.PI / 2); // вдоль X
  const pos = geometry.attributes.position;
  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i);
    pos.setY(i, pos.getY(i) - 1.1 * x * x); // дуга вверх
  }
  geometry.scale(1, 1, 0.55);
  geometry.computeVertexNormals();
  return geometry;
}

// Пучок шерсти: несколько конусов, расходящихся веером из одной точки.
const UP = new THREE.Vector3(0, 1, 0);

function createTuft(material, { count, length, radius, spread, direction }) {
  const group = new THREE.Group();
  const cone = new THREE.ConeGeometry(radius, length, 12);
  cone.translate(0, length / 2, 0); // основание в точке крепления

  for (let i = 0; i < count; i++) {
    const t = count === 1 ? 0 : i / (count - 1) - 0.5;
    const spike = new THREE.Mesh(cone, material);
    const k = 1 - Math.abs(t) * 0.5; // крайние пряди короче
    spike.scale.set(k, k, k);
    spike.rotation.z = -t * spread;
    group.add(spike);
  }
  // Ось Y пучка разворачиваем в нужную сторону
  group.quaternion.setFromUnitVectors(UP, direction.clone().normalize());
  return group;
}

function createEye(side) {
  const eye = new THREE.Group();
  eye.name = side > 0 ? 'eyeL' : 'eyeR';

  // Белок — вытянутый вверх эллипсоид, наполовину утопленный в череп
  const white = new THREE.Mesh(new THREE.SphereGeometry(1, 32, 24), toonMaterial(C.eyeWhite));
  white.scale.set(0.22, 0.3, 0.2);
  white.name = 'white';
  eye.add(white);

  // Зрачок лежит на передней поверхности белка; группа нужна, чтобы
  // потом двигать взгляд, вращая её вокруг центра глаза
  const look = new THREE.Group();
  look.name = 'look';
  eye.add(look);

  const pupil = new THREE.Mesh(new THREE.SphereGeometry(1, 24, 16), toonMaterial(C.pupil));
  pupil.scale.set(0.085, 0.115, 0.05);
  pupil.position.set(-side * 0.035, -0.02, 0.175);
  pupil.name = 'pupil';
  look.add(pupil);

  const glint = new THREE.Mesh(new THREE.SphereGeometry(1, 12, 8), flatMaterial(0xffffff));
  glint.scale.setScalar(0.026);
  glint.position.set(-side * 0.035 + 0.03, 0.025, 0.22);
  look.add(glint);

  return eye;
}

export function createCoyoteHead() {
  const head = new THREE.Group();
  head.name = 'coyoteHead';

  const furMat = toonMaterial(C.fur);
  const darkMat = toonMaterial(C.furDark);

  // --- Череп: светлый подбородок снизу, граница слегка волнистая ---
  const skull = new THREE.Mesh(
    new THREE.SphereGeometry(0.72, 48, 32),
    twoToneMaterial({
      top: C.fur,
      bottom: C.cream,
      normal: new THREE.Vector3(0, 1, -0.35),
      offset: -0.38,
      waveDir: new THREE.Vector3(1, 0, 0),
      waveAmp: 0.04,
      waveFreq: 14,
    })
  );
  skull.scale.set(1, 0.95, 1.08); // чуть вытянут назад
  skull.name = 'skull';
  head.add(skull);

  // --- Морда: сверху рыжая, «брюшко» снизу и по бокам светлое ---
  const snout = new THREE.Mesh(
    createSnoutGeometry(),
    twoToneMaterial({
      top: C.fur,
      bottom: C.cream,
      normal: new THREE.Vector3(0, 1, 0.12),
      offset: -0.02,
      waveDir: new THREE.Vector3(0, 0, 1),
      waveAmp: 0.025,
      waveFreq: 18,
    })
  );
  snout.position.set(0, -0.02, 0.42);
  snout.rotation.x = 0.16; // кончик слегка опущен
  snout.name = 'snout';
  head.add(snout);

  // --- Нос-пуговица на самом кончике ---
  const nose = new THREE.Mesh(new THREE.SphereGeometry(0.13, 32, 24), toonMaterial(C.nose));
  nose.scale.set(1.15, 0.9, 0.95);
  nose.position.set(0, 0.035, 1.08);
  nose.name = 'nose';
  snout.add(nose);

  const noseGlint = new THREE.Mesh(new THREE.SphereGeometry(0.035, 12, 8), flatMaterial(0xffffff));
  noseGlint.scale.set(1.4, 0.8, 1);
  noseGlint.position.set(0.04, 0.09, 0.08);
  nose.add(noseGlint);

  // --- Глаза: большие, близко посаженные над переносицей ---
  for (const side of [1, -1]) {
    const eye = createEye(side);
    eye.position.set(side * 0.21, 0.24, 0.58);
    eye.rotation.set(-0.1, side * 0.42, side * -0.08); // чуть врозь и назад
    head.add(eye);
  }

  // --- Брови: отдельные объекты над глазами ---
  const browGeometry = createBrowGeometry();
  const browMat = toonMaterial(C.brow);
  for (const side of [1, -1]) {
    const brow = new THREE.Mesh(browGeometry, browMat);
    brow.name = side > 0 ? 'browL' : 'browR';
    brow.position.set(side * 0.23, 0.57, 0.64);
    brow.rotation.set(-0.25, side * 0.35, side * 0.22); // внутренний край ниже
    head.add(brow);
  }

  // --- Шерсть пучками: хохолок, загривок и бакенбарды держат силуэт
  // с любого ракурса, в том числе со спины ---
  const crest = createTuft(darkMat, {
    count: 3, length: 0.42, radius: 0.1, spread: 0.9,
    direction: new THREE.Vector3(0, 1, -0.45),
  });
  crest.position.set(0, 0.58, -0.15);
  crest.name = 'crest';
  head.add(crest);

  const nape = createTuft(darkMat, {
    count: 3, length: 0.4, radius: 0.12, spread: 1.0,
    direction: new THREE.Vector3(0, -0.45, -1),
  });
  nape.position.set(0, -0.2, -0.58);
  nape.name = 'nape';
  head.add(nape);

  for (const side of [1, -1]) {
    const cheek = createTuft(furMat, {
      count: 3, length: 0.55, radius: 0.14, spread: 0.8,
      direction: new THREE.Vector3(side, -0.3, -0.55),
    });
    cheek.position.set(side * 0.55, -0.25, -0.05);
    cheek.name = side > 0 ? 'cheekL' : 'cheekR';
    head.add(cheek);
  }

  // --- Уши: отдельные объекты, точка вращения у основания ---
  const ears = createEars({ furColor: C.fur, innerColor: C.earInner });
  for (const ear of ears) {
    const side = ear.userData.side;
    ear.position.set(side * 0.36, 0.48, -0.12); // основание утоплено в череп
    head.add(ear);
  }
  head.userData.ears = { left: ears[0], right: ears[1] };

  return head;
}
