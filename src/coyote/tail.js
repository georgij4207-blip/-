import * as THREE from 'three';
import { twoToneMaterial } from '../toon.js';
import { skinAlongY } from './limb.js';

// Пушистый хвост: одна SkinnedMesh на цепочке костей, поэтому гнётся
// плавно. Иерархия:
//
//   tail (крепление на тазу, задаёт направление «назад-вниз»)
//   └─ pivot — поза хвоста целиком: подъём и взмах в сторону
//      ├─ mesh (SkinnedMesh)
//      └─ bone0 → bone1 → … → bone6 — изгиб и запаздывание
//
// Запаздывание — пружины на каждой кости (см. updateTail): когда основание
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

// Пружины запаздывания
const SPRING = {
  stiffness: 70, // возврат к позе
  damping: 7, // затухание колебаний
  coupling: 45, // кость тянется за предыдущей — волна к кончику
  inertia: 0.08, // насколько сильно линейное ускорение основания отклоняет кости
  turnInertia: 0.35, // то же для углового ускорения (койот резко повернулся)
  maxAngle: 0.9,
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
  tail.userData.spring = {
    angle: bones.map(() => new THREE.Vector2()), // отклонение (x — вверх/вниз, y — вбок)
    velocity: bones.map(() => new THREE.Vector2()),
    prevPos: null,
    prevVel: new THREE.Vector3(),
    prevQuat: new THREE.Quaternion(),
    prevOmega: new THREE.Vector3(),
  };
  applyTailPose(tail);
  return tail;
}

// Поза костей (bone.userData.posed) + отклонение пружин → повороты костей.
// Скелет персонажа пишет позу прямо в posed, пружина добавляется сверху.
function writeBones(tail) {
  const { bones } = tail.userData.parts;
  const { angle } = tail.userData.spring;
  bones.forEach((bone, i) => {
    const posed = bone.userData.posed;
    bone.rotation.set(posed.x + angle[i].x, posed.y, posed.z + angle[i].y);
  });
}

// Высокоуровневая поза: подъём и взмах — шарнир, изгиб — поровну по костям.
export function applyTailPose(tail, pose = {}) {
  Object.assign(tail.userData.pose, pose);
  const { lift, swing, curl } = tail.userData.pose;
  const { pivot, bones } = tail.userData.parts;
  pivot.rotation.x = lift;
  pivot.rotation.z = -swing;
  bones.forEach((bone, i) => bone.userData.posed.set(i === 0 ? 0 : curl / (BONES - 1), 0, 0));
  writeBones(tail);
}

const _pos = new THREE.Vector3();
const _vel = new THREE.Vector3();
const _acc = new THREE.Vector3();
const _q = new THREE.Quaternion();
const _dq = new THREE.Quaternion();
const _omega = new THREE.Vector3();
const _alpha = new THREE.Vector3();

/**
 * Шаг пружинного запаздывания. Вызывать каждый кадр с dt в секундах.
 * Ускорение основания хвоста (в его локальных осях) толкает кости в
 * обратную сторону; пружины возвращают их к позе.
 */
export function updateTail(tail, dt) {
  const s = tail.userData.spring;
  const { pivot } = tail.userData.parts;
  dt = Math.min(dt, 1 / 20); // после паузы вкладки не «выстреливаем»

  pivot.getWorldPosition(_pos);
  pivot.getWorldQuaternion(_q);
  if (!s.prevPos) {
    s.prevPos = _pos.clone();
    s.prevQuat.copy(_q);
    return;
  }
  const step = Math.max(dt, 1e-4);

  // Линейное ускорение основания
  _vel.copy(_pos).sub(s.prevPos).divideScalar(step);
  _acc.copy(_vel).sub(s.prevVel).divideScalar(step);
  s.prevPos.copy(_pos);
  s.prevVel.copy(_vel);

  // Угловая скорость и ускорение основания — в осях шарнира хвоста
  _dq.copy(s.prevQuat).invert().multiply(_q); // поворот за кадр в локальных осях
  if (_dq.w < 0) _dq.set(-_dq.x, -_dq.y, -_dq.z, -_dq.w);
  const halfAngle = Math.acos(Math.min(_dq.w, 1));
  const sinHalf = Math.sin(halfAngle);
  if (sinHalf > 1e-6) _omega.set(_dq.x, _dq.y, _dq.z).multiplyScalar((2 * halfAngle) / sinHalf / step);
  else _omega.set(0, 0, 0);
  _alpha.copy(_omega).sub(s.prevOmega).divideScalar(step).clampLength(0, 400);
  s.prevOmega.copy(_omega);
  s.prevQuat.copy(_q);

  // Линейное ускорение — тоже в осях шарнира (Y — вдоль хвоста)
  _acc.applyQuaternion(_q.invert()).clampLength(0, 60);

  // Мелкие подшаги для устойчивости пружин
  const steps = Math.ceil(dt / (1 / 120));
  const h = dt / steps;
  for (let n = 0; n < steps; n++) {
    for (let i = 0; i < BONES; i++) {
      const a = s.angle[i];
      const v = s.velocity[i];
      const parent = i > 0 ? s.angle[i - 1] : null;
      const k = 1 + i * 0.35; // кончик болтается сильнее
      const push = SPRING.inertia * k;
      const turn = SPRING.turnInertia * k;

      // Хвост отстаёт: ускорение по +Z опускает его к −Z (поворот вокруг X
      // в минус), по +X — отклоняет к −X (поворот вокруг Z в плюс);
      // угловое ускорение основания хвост «не успевает» повторить
      let fx = -SPRING.stiffness * a.x - SPRING.damping * v.x - push * _acc.z - turn * _alpha.x;
      let fy = -SPRING.stiffness * a.y - SPRING.damping * v.y + push * _acc.x - turn * _alpha.z;
      if (parent) {
        fx += SPRING.coupling * (parent.x - a.x);
        fy += SPRING.coupling * (parent.y - a.y);
      }
      v.x += fx * h;
      v.y += fy * h;
      a.x = THREE.MathUtils.clamp(a.x + v.x * h, -SPRING.maxAngle, SPRING.maxAngle);
      a.y = THREE.MathUtils.clamp(a.y + v.y * h, -SPRING.maxAngle, SPRING.maxAngle);
    }
  }
  writeBones(tail);
}
