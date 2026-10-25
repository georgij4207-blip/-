import * as THREE from 'three';
import { twoToneMaterial } from '../toon.js';
import { skinAlongY } from './limb.js';
import { createSpringChain } from './spring.js';

// Пушистый хвост: одна SkinnedMesh на цепочке костей, поэтому гнётся
// плавно. Иерархия:
//
//   tail (крепление на тазу, задаёт направление «назад-вниз»)
//   └─ pivot — поза хвоста целиком: подъём и взмах в сторону
//      ├─ mesh (SkinnedMesh)
//      └─ bone0 → bone1 → … → bone6 — изгиб и запаздывание
//
// Запаздывание — пружины на каждой кости (spring.js): когда основание
// хвоста дёргается, кости отклоняются в обратную сторону и с затуханием
// возвращаются к позе, причём волна идёт от основания к кончику.

export const TAIL_LENGTH = 2.0;
const BONES = 7;
const BONE_HEIGHTS = Array.from({ length: BONES }, (_, i) => (i * TAIL_LENGTH) / BONES);
const TIP_START = TAIL_LENGTH * 0.74; // откуда начинается тёмный кончик

// Поза по умолчанию (радианы); её же возвращает «Сброс»
export const DEFAULT_TAIL_POSE = {
  lift: 0, // + выше, − ниже (относительно «назад-вниз» в покое)
  swing: 0, // + в левую сторону койота, − в правую
  curl: 0.7, // изгиб по всей длине: + кончик вверх, − вниз
};


// Профиль: [доля длины, радиус] — тонкое основание, пышная середина, острый кончик
const PROFILE = [
  [0.0, 0.08],
  [0.08, 0.15],
  [0.25, 0.29],
  [0.45, 0.37],
  [0.65, 0.36],
  [0.82, 0.27],
  [0.93, 0.15],
  [1.0, 0.0],
];

function createTailGeometry() {
  const curve = new THREE.SplineCurve(PROFILE.map(([t, r]) => new THREE.Vector2(r, t * TAIL_LENGTH)));
  const points = curve.getPoints(48);
  points[0].x = PROFILE[0][1];
  points[points.length - 1].set(0, TAIL_LENGTH); // острый кончик
  points.unshift(new THREE.Vector2(0, 0)); // закрытое основание

  const geometry = new THREE.LatheGeometry(points, 40);
  // Нормали берём с гладкой формы: заливка остаётся чистой, а «шерсть»
  // ниже меняет только силуэт (контур становится рваным)
  geometry.computeVertexNormals();
  const pos = geometry.attributes.position;
  for (let i = 0; i < pos.count; i++) {
    const x = pos.getX(i);
    const y = pos.getY(i);
    const z = pos.getZ(i);
    // «Шерсть»: продольные валики, слегка закрученные по длине, и уступы-
    // пряди, которые расширяются к кончику и резко обрываются
    const angle = Math.atan2(z, x);
    const tuft = (y * 2.8 + 0.15 * Math.sin(angle * 3)) % 1;
    const fluff = 1 + 0.08 * Math.sin(angle * 7 + y * 3) + 0.2 * tuft * tuft * tuft;
    pos.setXYZ(i, x * fluff, y, z * fluff * 0.9); // чуть сплющен с боков
  }
  return skinAlongY(geometry, BONE_HEIGHTS);
}

export function createTail(colors) {
  const tail = new THREE.Group();
  tail.name = 'tail';

  const pivot = new THREE.Group();
  pivot.name = 'pivot';
  pivot.rotation.order = 'ZXY';
  tail.add(pivot);

  const bones = BONE_HEIGHTS.map((h, i) => {
    const bone = new THREE.Bone();
    bone.name = `bone${i}`;
    bone.position.y = i === 0 ? 0 : TAIL_LENGTH / BONES;
    // Поза кости без пружины; итоговый поворот = posed + отклонение пружины
    bone.userData.posed = new THREE.Euler();
    return bone;
  });
  for (let i = 1; i < BONES; i++) bones[i - 1].add(bones[i]);
  pivot.add(bones[0]);

  // Тёмный кончик: всё, что дальше TIP_START вдоль хвоста, с «рваной» границей
  const material = twoToneMaterial({
    top: colors.fur,
    bottom: colors.tailTip,
    normal: new THREE.Vector3(0, -1, 0),
    offset: -TIP_START,
    waveDir: new THREE.Vector3(1, 0, 0.6),
    waveAmp: 0.05,
    waveFreq: 30,
  });
  const mesh = new THREE.SkinnedMesh(createTailGeometry(), material);
  mesh.name = 'mesh';
  pivot.add(mesh);

  pivot.updateMatrixWorld(true);
  mesh.bind(new THREE.Skeleton(bones));

  tail.userData.parts = { pivot, bones, mesh };
  tail.userData.pose = { ...DEFAULT_TAIL_POSE };
  tail.userData.spring = createSpringChain(pivot, bones, {
    stiffness: 60, damping: 6, coupling: 45, inertia: 0.3, turnInertia: 0.35, droop: 0.15,
  });
  applyTailPose(tail);
  return tail;
}

// Высокоуровневая поза: подъём и взмах — шарнир, изгиб — поровну по костям.
export function applyTailPose(tail, pose = {}) {
  Object.assign(tail.userData.pose, pose);
  const { lift, swing, curl } = tail.userData.pose;
  const { pivot, bones } = tail.userData.parts;
  pivot.rotation.x = lift;
  pivot.rotation.z = -swing;
  bones.forEach((bone, i) => bone.userData.posed.set(i === 0 ? 0 : curl / (BONES - 1), 0, 0));
  tail.userData.spring.write();
}

// Шаг пружинного запаздывания хвоста (см. spring.js). Вызывать каждый кадр.
export function updateTail(tail, dt) {
  tail.userData.spring.update(dt);
}
