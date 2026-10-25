import * as THREE from 'three';
import { twoToneMaterial } from '../toon.js';

// Тело койота: тощий вытянутый торс с выпяченной грудью и узкой талией,
// светлое пузо спереди и тонкая шея. Начало координат торса — таз
// (низ торса), ось Y вверх, перёд — +Z.

const PROFILE_HEIGHT = 1.85;
const STRETCH = 1.2; // вытягиваем торс по высоте
export const TORSO_HEIGHT = PROFILE_HEIGHT * STRETCH;
export const NECK_LENGTH = 0.55;

// Профиль торса снизу вверх: [высота, радиус]
const TORSO_PROFILE = [
  [0.0, 0.0],
  [0.06, 0.17],
  [0.18, 0.25], // таз
  [0.32, 0.26],
  [0.55, 0.19], // талия
  [0.75, 0.2],
  [1.0, 0.3],
  [1.25, 0.42], // грудь
  [1.45, 0.44],
  [1.62, 0.38],
  [1.76, 0.24], // плечи сходятся к шее
  [PROFILE_HEIGHT, 0.0],
];

// Плавный «бугор» вокруг center шириной width (0 вдали, 1 в центре)
function bump(y, center, width) {
  const t = (y - center) / width;
  return Math.exp(-t * t);
}

function createTorsoGeometry() {
  const profile = TORSO_PROFILE.map(([y, r]) => new THREE.Vector2(r, y));
  const geometry = new THREE.LatheGeometry(profile, 48);
  const pos = geometry.attributes.position;

  for (let i = 0; i < pos.count; i++) {
    const y = pos.getY(i);
    let x = pos.getX(i) * 1.05; // чуть шире, чем глубже
    let z = pos.getZ(i) * 0.78;

    // Грудь колесом: передняя сторона на уровне груди выпячивается
    if (z > 0) z *= 1 + 0.45 * bump(y, 1.35, 0.3);
    // S-образная спина: грудь вперёд, талия чуть назад
    z += 0.12 * bump(y, 1.35, 0.35) - 0.06 * bump(y, 0.6, 0.25);
    // Узкие плечи у тощего койота
    x *= 1 - 0.12 * bump(y, 1.62, 0.15);

    pos.setXYZ(i, x, y * STRETCH, z);
  }
  geometry.computeVertexNormals();
  return geometry;
}

// Светлое пузо и горло: передний сектор шириной ±wedge вокруг оси.
// Волна по высоте даёт «шерстяную» границу.
function bellyMaterial(colors, wedge) {
  return twoToneMaterial({
    top: colors.fur,
    bottom: colors.cream,
    wedge,
    waveDir: new THREE.Vector3(0, 1, 0),
    waveAmp: 0.04,
    waveFreq: 12,
  });
}

export function createTorso(colors) {
  const torso = new THREE.Mesh(createTorsoGeometry(), bellyMaterial(colors, 0.55));
  torso.name = 'torso';
  return torso;
}

// Шея — тонкий слегка сужающийся цилиндр от основания (y = 0) вверх.
// Концы заходят в грудь и в череп, чтобы стыков не было видно.
export function createNeck(colors) {
  const length = NECK_LENGTH + 0.35;
  const geometry = new THREE.CylinderGeometry(0.13, 0.17, length, 24, 4);
  geometry.translate(0, length / 2 - 0.15, 0);
  geometry.scale(1, 1, 0.9);
  const neck = new THREE.Mesh(geometry, bellyMaterial(colors, 0.7));
  neck.name = 'neckMesh';
  return neck;
}
