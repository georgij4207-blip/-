import * as THREE from 'three';
import { toonMaterial } from '../toon.js';
import { skinAlongY } from './limb.js';

// Уши койота. Иерархия одного уха:
//
//   earL (крепление на черепе, точка вращения у основания)
//   └─ pivot — поза уха целиком: наклон, развал, поворот
//      ├─ outer (SkinnedMesh, шерсть)   ┐ общий скелет:
//      ├─ inner (SkinnedMesh, розовое)  ┘ root → mid → tip
//      └─ root (Bone) → mid (Bone) → tip (Bone) — изгиб уха
//
// Кости дают плавный изгиб, а позже на них же можно повесить пружинное
// запаздывание при движении головы.

export const EAR_HEIGHT = 0.9;
const BONE_HEIGHTS = [0, EAR_HEIGHT * 0.4, EAR_HEIGHT * 0.75];

// Поза по умолчанию (радианы); её же возвращает кнопка «Сброс»
export const DEFAULT_EAR_POSE = {
  tilt: -0.15, // + вперёд, − назад (прижать к голове)
  splay: 0.3, // + наружу, − внутрь
  twist: 0.3, // + раковиной наружу, − внутрь
  bend: 0.1, // + кончик вперёд/вниз, − назад
};

// Внешняя часть: заострённый конус, сплющенный спереди назад,
// с почти плоской передней стороной под розовую вставку.
function createOuterGeometry() {
  const geometry = new THREE.ConeGeometry(0.27, EAR_HEIGHT, 24, 12);
  geometry.translate(0, EAR_HEIGHT / 2, 0); // основание в начале координат
  geometry.scale(1, 1, 0.42);
  const pos = geometry.attributes.position;
  for (let i = 0; i < pos.count; i++) {
    if (pos.getZ(i) > 0) pos.setZ(i, pos.getZ(i) * 0.3);
  }
  geometry.computeVertexNormals();
  return skinAlongY(geometry, BONE_HEIGHTS);
}

// Внутренняя часть: тонкая розовая «капля», чуть выступает над передней стороной.
function createInnerGeometry() {
  const height = EAR_HEIGHT * 0.78;
  const geometry = new THREE.ConeGeometry(0.17, height, 20, 10);
  geometry.translate(0, height / 2 + 0.07, 0.035);
  const pos = geometry.attributes.position;
  for (let i = 0; i < pos.count; i++) {
    pos.setZ(i, 0.035 + (pos.getZ(i) - 0.035) * 0.12);
  }
  geometry.computeVertexNormals();
  return skinAlongY(geometry, BONE_HEIGHTS);
}

function createEar(side, { furColor, innerColor }) {
  const ear = new THREE.Group();
  ear.name = side > 0 ? 'earL' : 'earR';
  ear.userData.side = side;

  const pivot = new THREE.Group();
  pivot.name = 'pivot';
  pivot.rotation.order = 'ZXY'; // сначала поворот вокруг своей оси, потом наклон и развал
  ear.add(pivot);

  const root = new THREE.Bone();
  root.name = 'root';
  const mid = new THREE.Bone();
  mid.name = 'mid';
  mid.position.y = BONE_HEIGHTS[1];
  const tip = new THREE.Bone();
  tip.name = 'tip';
  tip.position.y = BONE_HEIGHTS[2] - BONE_HEIGHTS[1];
  root.add(mid);
  mid.add(tip);
  pivot.add(root);

  const outer = new THREE.SkinnedMesh(createOuterGeometry(), toonMaterial(furColor));
  outer.name = 'outer';
  const inner = new THREE.SkinnedMesh(createInnerGeometry(), toonMaterial(innerColor));
  inner.name = 'inner';
  pivot.add(outer, inner);

  pivot.updateMatrixWorld(true);
  const skeleton = new THREE.Skeleton([root, mid, tip]);
  outer.bind(skeleton);
  inner.bind(skeleton);

  ear.userData.parts = { pivot, root, mid, tip };
  ear.userData.pose = { ...DEFAULT_EAR_POSE };
  applyEarPose(ear);
  return ear;
}

// Применяет ear.userData.pose (или переданные поля) к костям и шарниру.
export function applyEarPose(ear, pose = {}) {
  Object.assign(ear.userData.pose, pose);
  const { tilt, splay, twist, bend } = ear.userData.pose;
  const { pivot, mid, tip } = ear.userData.parts;
  const side = ear.userData.side;

  pivot.rotation.x = tilt;
  pivot.rotation.y = side * twist;
  pivot.rotation.z = -side * splay;

  // Изгиб распределён по суставам, к кончику сильнее
  mid.rotation.x = bend * 0.45;
  tip.rotation.x = bend * 0.65;
}

export function createEars(colors) {
  return [1, -1].map((side) => createEar(side, colors));
}
