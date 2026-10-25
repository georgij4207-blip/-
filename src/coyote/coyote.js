import * as THREE from 'three';
import { createCoyoteHead, COYOTE_COLORS } from './head.js';
import { createTorso, createNeck, NECK_LENGTH, TORSO_HEIGHT } from './body.js';
import { createArms } from './arms.js';
import { createLegs, solveLegToFloor, ANKLE_HEIGHT } from './legs.js';
import { createTail, updateTail } from './tail.js';
import { createSkeleton } from './skeleton.js';
import { createFace } from './face.js';

// Сборка персонажа. Иерархия (все группы — точки вращения):
//
//   coyote (на полу)
//   └─ hips — таз
//      ├─ torso
//      ├─ tail — пушистый хвост на цепочке костей с запаздыванием (см. tail.js)
//      ├─ legL / legR — бедро → колено → голеностоп → стопа → пальцы (см. legs.js)
//      ├─ armL / armR — плечо → локоть → запястье → кисть (см. arms.js)
//      └─ neckPivot — основание шеи на груди
//         ├─ neckMesh
//         └─ headPivot — верх шеи, отдельная точка вращения головы
//            └─ coyoteHead
//
// Повороты neckPivot и headPivot по умолчанию нулевые: вся «поза покоя»
// зашита в положение дочерних объектов, поэтому для анимации достаточно
// крутить шарниры от нуля.

export const HIP_HEIGHT = 2.05; // высота таза; ноги подгоняются под неё по IK
const NECK_TILT = 0.4; // шея тянется вперёд от груди
const HEAD_ATTACH = new THREE.Vector3(0, -0.52, -0.28); // куда шея входит в череп (в координатах головы)

export function createCoyote() {
  const coyote = new THREE.Group();
  coyote.name = 'coyote';

  const hips = new THREE.Group();
  hips.name = 'hips';
  hips.position.y = HIP_HEIGHT;
  hips.rotation.x = 0.06; // лёгкая сутулость вперёд
  coyote.add(hips);

  hips.add(createTorso(COYOTE_COLORS));

  // --- Руки: плечевые суставы по бокам верха груди ---
  const arms = createArms(COYOTE_COLORS);
  for (const arm of arms) {
    arm.position.set(arm.userData.side * 0.36, TORSO_HEIGHT - 0.32, 0.05);
    hips.add(arm);
  }

  // --- Шея ---
  const neckRest = new THREE.Group(); // положение и наклон шеи в покое
  neckRest.position.set(0, TORSO_HEIGHT - 0.18, 0.14);
  neckRest.rotation.x = NECK_TILT;
  hips.add(neckRest);

  const neckPivot = new THREE.Group();
  neckPivot.name = 'neckPivot';
  neckRest.add(neckPivot);
  neckPivot.add(createNeck(COYOTE_COLORS));

  // --- Голова на отдельном шарнире ---
  const headPivot = new THREE.Group();
  headPivot.name = 'headPivot';
  headPivot.position.y = NECK_LENGTH;
  neckPivot.add(headPivot);

  const head = createCoyoteHead();
  // Выравниваем голову (компенсируем наклоны таза и шеи) и сдвигаем так,
  // чтобы точка входа шеи в череп совпала с шарниром
  head.rotation.x = -(NECK_TILT + hips.rotation.x);
  head.position.copy(HEAD_ATTACH).applyEuler(head.rotation).negate();
  headPivot.add(head);

  // --- Ноги: крепятся по бокам таза и ставятся на пол обратной кинематикой ---
  const legs = createLegs(COYOTE_COLORS);
  const ankleTarget = new THREE.Vector3();
  for (const leg of legs) {
    leg.position.set(leg.userData.side * 0.16, 0.16, -0.02);
    hips.add(leg);
    // Голеностоп под тазобедренным суставом, чуть позади: длинные стопы
    // уходят вперёд под грудь и голову — так он устойчив
    leg.getWorldPosition(ankleTarget);
    ankleTarget.set(ankleTarget.x, ANKLE_HEIGHT, ankleTarget.z - 0.2);
    leg.userData.standPose = solveLegToFloor(leg, ankleTarget);
    leg.userData.plant = ankleTarget.clone(); // точка стопы на полу (координаты койота)
  }

  // --- Хвост: из задней части таза назад-вниз ---
  const tail = createTail(COYOTE_COLORS);
  tail.position.set(0, 0.34, -0.18);
  tail.rotation.x = -2.35; // ось хвоста (+Y) смотрит назад и вниз
  hips.add(tail);

  // --- Мимика: веки, рот, зрачки, эффекты (см. face.js) ---
  const face = createFace(head, COYOTE_COLORS);
  face.setSootRoot(coyote);

  coyote.userData.parts = { hips, neckPivot, headPivot, head };
  coyote.userData.face = face;
  coyote.userData.tail = tail;
  coyote.userData.legs = { left: legs[0], right: legs[1] };
  coyote.userData.ears = head.userData.ears;
  coyote.userData.arms = { left: arms[0], right: arms[1] };

  // Единый скелет: все суставы, позы, «стопы на полу» (см. skeleton.js)
  const skeleton = createSkeleton(coyote);
  coyote.userData.skeleton = skeleton;

  // Вызывать каждый кадр: переходы между позами и запаздывание хвоста.
  // Возвращает true, пока идёт переход между позами
  coyote.userData.update = (dt) => {
    const posing = skeleton.update(dt);
    updateTail(tail, dt);
    face.update(dt);
    return posing;
  };
  return coyote;
}
