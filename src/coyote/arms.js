import * as THREE from 'three';
import { toonMaterial } from '../toon.js';

// Передние лапы-руки. Каждая часть — отдельный сустав (Group), сегмент
// висит от сустава вдоль его локальной −Y:
//
//   armL (крепление на плече)
//   └─ shoulder — плечевой сустав
//      ├─ upperArm (меш)
//      └─ elbow — локоть
//         ├─ forearm (меш)
//         └─ wrist — запястье
//            └─ hand (ладонь развёрнута к телу)
//               ├─ palm (меш)
//               ├─ grip — точка, куда вешать предметы
//               ├─ finger0..2: base → mid — по два сустава на палец
//               └─ thumb: base → mid — противопоставленный большой палец
//
// Все суставы в покое повёрнуты через applyArmPose, поэтому позой
// управляют только параметры ниже.

const UPPER_ARM = { length: 0.8, rTop: 0.075, rBottom: 0.06 };
const FOREARM = { length: 0.78, rTop: 0.062, rBottom: 0.045 };
const FINGER = { base: 0.15, tip: 0.12, radius: 0.034 };

// Поза по умолчанию (радианы); её же возвращает «Сброс»
export const DEFAULT_ARM_POSE = {
  raise: 0.15, // плечо: + вперёд, − назад
  spread: 0.18, // плечо: + в сторону от тела
  twist: 0, // плечо: поворот руки вокруг своей оси (+ наружу)
  elbow: 0.45, // + сгиб локтя
  wrist: 0, // + кисть к ладони, − назад
  curl: 0.35, // сжатие пальцев: 0 — раскрыты, ~1.5 — кулак
  thumb: 0.3, // большой палец к ладони
};

// Сегмент конечности: вытянутая «капсула» от 0 до −length с разными
// радиусами концов. Скруглённые концы прячут стыки в суставах.
function createSegmentGeometry({ length, rTop, rBottom }) {
  const points = [];
  const cap = 8;
  for (let i = 0; i <= cap; i++) {
    const a = -Math.PI / 2 + (i / cap) * (Math.PI / 2); // низ: −90° → 0°
    points.push(new THREE.Vector2(Math.cos(a) * rBottom, -length + Math.sin(a) * rBottom));
  }
  for (let i = 0; i <= cap; i++) {
    const a = (i / cap) * (Math.PI / 2); // верх: 0° → 90°
    points.push(new THREE.Vector2(Math.cos(a) * rTop, Math.sin(a) * rTop));
  }
  return new THREE.LatheGeometry(points, 20);
}

function joint(name, y = 0) {
  const group = new THREE.Group();
  group.name = name;
  group.position.y = y;
  return group;
}

// Палец: два сустава и два сегмента, кончик потоньше
function createFinger(name, material, geometries, scale = 1) {
  const base = joint(name);
  const first = new THREE.Mesh(geometries.base, material);
  first.scale.setScalar(scale);
  base.add(first);

  const mid = joint('mid', -FINGER.base * scale);
  const second = new THREE.Mesh(geometries.tip, material);
  second.scale.setScalar(scale);
  mid.add(second);
  base.add(mid);

  return { base, mid };
}

function createHand(side, material) {
  const hand = new THREE.Group();
  hand.name = 'hand';
  // Ладонь (локальная +Z) смотрит к телу, большой палец — вперёд
  hand.rotation.y = -side * Math.PI / 2;

  const palm = new THREE.Mesh(new THREE.SphereGeometry(1, 24, 16), material);
  palm.scale.set(0.12, 0.15, 0.065);
  palm.position.y = -0.12;
  palm.name = 'palm';
  hand.add(palm);

  // Сюда крепятся предметы: перед ладонью, между пальцами
  const grip = new THREE.Object3D();
  grip.name = 'grip';
  grip.position.set(0, -0.26, 0.1);
  hand.add(grip);

  const geometries = {
    base: createSegmentGeometry({ length: FINGER.base, rTop: FINGER.radius, rBottom: FINGER.radius * 0.95 }),
    tip: createSegmentGeometry({ length: FINGER.tip, rTop: FINGER.radius * 0.95, rBottom: FINGER.radius * 0.85 }),
  };

  // Три пальца веером вдоль нижнего края ладони; средний длиннее
  const fingers = [-1, 0, 1].map((k, i) => {
    const finger = createFinger(`finger${i}`, material, geometries, k === 0 ? 1.08 : 0.95);
    finger.base.position.set(side * k * 0.072, -0.24, 0.0);
    finger.base.rotation.z = -side * k * 0.12; // лёгкий веер
    hand.add(finger.base);
    return finger;
  });

  // Большой палец сбоку ладони, ближе к запястью, смотрит вперёд-вниз
  const thumb = createFinger('thumb', material, geometries, 0.85);
  thumb.base.position.set(side * 0.1, -0.1, 0.02);
  thumb.base.rotation.order = 'ZYX';
  thumb.base.userData.restZ = side * 0.9;
  hand.add(thumb.base);

  return { hand, grip, fingers, thumb };
}

function createArm(side, colors) {
  const arm = new THREE.Group();
  arm.name = side > 0 ? 'armL' : 'armR';
  arm.userData.side = side;

  const furMat = toonMaterial(colors.fur);
  const handMat = toonMaterial(colors.cream);

  const shoulder = joint('shoulder');
  shoulder.rotation.order = 'ZXY'; // поворот вокруг оси руки, потом вперёд, потом в сторону
  arm.add(shoulder);
  const upper = new THREE.Mesh(createSegmentGeometry(UPPER_ARM), furMat);
  upper.name = 'upperArm';
  shoulder.add(upper);

  const elbow = joint('elbow', -UPPER_ARM.length);
  shoulder.add(elbow);
  const forearm = new THREE.Mesh(createSegmentGeometry(FOREARM), furMat);
  forearm.name = 'forearm';
  elbow.add(forearm);

  const wrist = joint('wrist', -FOREARM.length);
  elbow.add(wrist);
  const { hand, grip, fingers, thumb } = createHand(side, handMat);
  wrist.add(hand);

  arm.userData.joints = { shoulder, elbow, wrist, hand, grip, fingers, thumb };
  arm.userData.pose = { ...DEFAULT_ARM_POSE };
  applyArmPose(arm);
  return arm;
}

// Применяет arm.userData.pose (или переданные поля) к суставам.
export function applyArmPose(arm, pose = {}) {
  Object.assign(arm.userData.pose, pose);
  const p = arm.userData.pose;
  const side = arm.userData.side;
  const { shoulder, elbow, wrist, fingers, thumb } = arm.userData.joints;

  // Рука висит вдоль −Y: поворот вокруг −X уводит её вперёд (+Z),
  // вокруг Z — в сторону от тела
  shoulder.rotation.set(-p.raise, side * p.twist, side * p.spread);
  elbow.rotation.x = -p.elbow; // предплечье складывается вперёд
  // Кисть развёрнута ладонью к телу, поэтому сгиб «к ладони» — вокруг Z
  wrist.rotation.z = -side * p.wrist;

  // Пальцы сгибаются к ладони (локальная +Z кисти)
  for (const { base, mid } of fingers) {
    base.rotation.x = -p.curl;
    mid.rotation.x = -p.curl * 1.2;
  }
  thumb.base.rotation.set(-0.5 - p.thumb * 0.6, 0, thumb.base.userData.restZ - side * p.thumb * 0.5);
  thumb.mid.rotation.x = -p.thumb * 1.1;
}

export function createArms(colors) {
  return [1, -1].map((side) => createArm(side, colors));
}
