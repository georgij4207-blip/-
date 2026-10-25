import * as THREE from 'three';
import { toonMaterial } from '../toon.js';
import { createSegmentGeometry, joint } from './limb.js';

// Задние ноги «как у собаки»: бедро идёт от таза вниз-назад, колено
// смотрит назад, голень возвращается вниз-вперёд, а длинная стопа лежит
// на полу и выдаётся вперёд — в беге она красиво перекатывается с пятки
// на пальцы. Каждая часть — отдельный сустав, сегмент висит вдоль −Y:
//
//   legL (крепление в тазу)
//   └─ hip — тазобедренный сустав
//      ├─ thigh (меш)
//      └─ knee — колено
//         ├─ shin (меш)
//         └─ ankle — голеностоп
//            └─ foot (стопа вперёд по +Z, подошва на высоте −ANKLE_HEIGHT)
//               └─ toes — сустав пальцев для переката в беге

const THIGH = { length: 1.1, rTop: 0.11, rBottom: 0.065 };
const SHIN = { length: 1.2, rTop: 0.07, rBottom: 0.05 };
export const ANKLE_HEIGHT = 0.16; // голеностоп над подошвой
const FOOT_LENGTH = 0.8; // от голеностопа до сустава пальцев

// Поза: углы в радианах.
//   hip   — + бедро вперёд, − назад
//   knee  — сгиб колена (колено уходит назад)
//   ankle — + носок вверх, − носок вниз
//   toes  — + пальцы вверх (перекат), − вниз
export const ZERO_LEG_POSE = { hip: 0, knee: 0, ankle: 0, toes: 0 };

function createFoot(side, material) {
  const foot = new THREE.Group();
  foot.name = 'foot';
  foot.rotation.y = side * 0.18; // носки слегка врозь

  // Вытянутая ступня-«лыжа»: пятка чуть позади голеностопа
  const sole = new THREE.Mesh(new THREE.SphereGeometry(1, 32, 16), material);
  sole.scale.set(0.19, 0.09, FOOT_LENGTH / 2 + 0.1);
  sole.position.set(0, -ANKLE_HEIGHT + 0.09, FOOT_LENGTH / 2 - 0.08);
  sole.name = 'sole';
  foot.add(sole);

  // Пальцы на отдельном суставе в передней части стопы
  const toes = joint('toes');
  toes.position.set(0, -ANKLE_HEIGHT + 0.065, FOOT_LENGTH);
  foot.add(toes);

  const toeGeometry = createSegmentGeometry({ length: 0.16, rTop: 0.065, rBottom: 0.06 });
  toeGeometry.rotateX(-Math.PI / 2); // вдоль +Z
  for (const k of [-1, 0, 1]) {
    const toe = new THREE.Mesh(toeGeometry, material);
    toe.position.set(k * 0.095, 0, k === 0 ? 0.02 : -0.02);
    toe.rotation.y = k * 0.18; // лёгкий веер
    toe.scale.setScalar(k === 0 ? 1.1 : 1);
    toes.add(toe);
  }

  return { foot, toes };
}

function createLeg(side, colors) {
  const leg = new THREE.Group();
  leg.name = side > 0 ? 'legL' : 'legR';
  leg.userData.side = side;

  const furMat = toonMaterial(colors.fur);
  const footMat = toonMaterial(colors.cream);

  const hip = joint('hip');
  leg.add(hip);
  const thigh = new THREE.Mesh(createSegmentGeometry(THIGH), furMat);
  thigh.name = 'thigh';
  hip.add(thigh);

  const knee = joint('knee', -THIGH.length);
  hip.add(knee);
  const shin = new THREE.Mesh(createSegmentGeometry(SHIN), furMat);
  shin.name = 'shin';
  knee.add(shin);

  const ankle = joint('ankle', -SHIN.length);
  knee.add(ankle);
  const { foot, toes } = createFoot(side, footMat);
  ankle.add(foot);

  leg.userData.joints = { hip, knee, ankle, foot, toes };
  leg.userData.pose = { ...ZERO_LEG_POSE };
  return leg;
}

// Прямая кинематика: углы позы → повороты суставов.
export function applyLegPose(leg, pose = {}) {
  Object.assign(leg.userData.pose, pose);
  const p = leg.userData.pose;
  const { hip, knee, ankle, toes } = leg.userData.joints;
  // Кость висит вдоль −Y: поворот вокруг +X уводит её назад (−Z)
  hip.rotation.x = -p.hip;
  knee.rotation.x = -p.knee; // голень складывается вперёд, колено — назад
  ankle.rotation.x = -p.ankle;
  toes.rotation.x = -p.toes;
}

/**
 * Обратная кинематика «встать на пол»: подбирает бедро и колено так, чтобы
 * голеностоп оказался в точке `ankleTarget` (мировые координаты), а стопа
 * легла ровно (подошва параллельна полу). Работает в плоскости ноги (Y–Z).
 * footPitch (радианы) наклоняет стопу от ровного положения: + носок вверх
 * (удар пяткой), − носок вниз (толчок). Возвращает получившуюся позу.
 */
const _target = new THREE.Vector3();
const _parentQ = new THREE.Quaternion();

export function solveLegToFloor(leg, ankleTarget, footPitch = 0) {
  const { hip } = leg.userData.joints;
  leg.updateWorldMatrix(true, false);
  _target.copy(ankleTarget);
  leg.worldToLocal(_target); // цель в координатах крепления ноги

  const L1 = THIGH.length;
  const L2 = SHIN.length;
  const d = THREE.MathUtils.clamp(Math.hypot(_target.y, _target.z), Math.abs(L1 - L2) + 1e-3, L1 + L2 - 1e-3);

  // Угол направления на цель: 0 — прямо вниз, + вперёд
  const toTarget = Math.atan2(_target.z, -_target.y);
  const a = Math.acos((L1 * L1 + d * d - L2 * L2) / (2 * L1 * d)); // бедро от линии на цель
  const b = Math.acos((L2 * L2 + d * d - L1 * L1) / (2 * L2 * d)); // голень от линии на цель

  // Колено назад: бедро отклоняется назад от линии на цель, голень — вперёд
  const pose = { hip: toTarget - a, knee: a + b };

  // Стопа горизонтально: суммарный наклон цепочки и родителей должен быть нулём
  leg.getWorldQuaternion(_parentQ);
  const parentPitch = new THREE.Euler().setFromQuaternion(_parentQ, 'YXZ').x; // наклон без учёта разворота
  // Повороты hip/knee/ankle вокруг X: −hip − knee − ankle + parentPitch = 0
  pose.ankle = parentPitch - pose.hip - pose.knee + footPitch;

  // Пишем только бедро, колено и голеностоп: пальцы и отведение бедра
  // в сторону остаются такими, какими их поставили
  Object.assign(leg.userData.pose, pose);
  const { knee, ankle } = leg.userData.joints;
  hip.rotation.x = -pose.hip;
  knee.rotation.x = -pose.knee;
  ankle.rotation.x = -pose.ankle;
  hip.updateWorldMatrix(false, true);
  return { ...leg.userData.pose };
}

export function createLegs(colors) {
  return [1, -1].map((side) => createLeg(side, colors));
}
