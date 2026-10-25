import * as THREE from 'three';
import { createCoyoteHead, COYOTE_COLORS } from './head.js';
import { createTorso, createNeck, NECK_LENGTH, TORSO_HEIGHT } from './body.js';

// Сборка персонажа. Иерархия (все группы — точки вращения):
//
//   coyote (на полу)
//   └─ hips — таз, сюда позже крепятся ноги
//      ├─ torso
//      └─ neckPivot — основание шеи на груди
//         ├─ neckMesh
//         └─ headPivot — верх шеи, отдельная точка вращения головы
//            └─ coyoteHead
//
// Повороты neckPivot и headPivot по умолчанию нулевые: вся «поза покоя»
// зашита в положение дочерних объектов, поэтому для анимации достаточно
// крутить шарниры от нуля.

export const HIP_HEIGHT = 1.6; // место под будущие ноги
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

  coyote.userData.parts = { hips, neckPivot, headPivot, head };
  coyote.userData.ears = head.userData.ears;
  return coyote;
}
