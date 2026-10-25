import * as THREE from 'three';

// Пружинное запаздывание для цепочки костей (хвост, уши).
//
// У каждой кости есть «поза» — bone.userData.posed (Euler), её пишет скелет.
// Итоговый поворот = posed + отклонение пружины. Пружину толкают
// ускорения основания цепочки (объект frame): линейное — кости отстают в
// обратную сторону, угловое — не успевают повторить поворот, вертикальное
// (droop) — сгибаются, как уши при прыжке. Волна идёт от основания к кончику.

const _pos = new THREE.Vector3();
const _vel = new THREE.Vector3();
const _acc = new THREE.Vector3();
const _q = new THREE.Quaternion();
const _qi = new THREE.Quaternion();
const _dq = new THREE.Quaternion();
const _omega = new THREE.Vector3();
const _alpha = new THREE.Vector3();

export const DEFAULT_SPRING = {
  stiffness: 70, // возврат к позе
  damping: 7, // затухание колебаний
  coupling: 45, // кость тянется за предыдущей
  inertia: 0.08, // отклик на линейное ускорение (поперёк цепочки)
  turnInertia: 0.35, // отклик на угловое ускорение
  droop: 0, // отклик на ускорение вдоль цепочки: + сгиб назад
  tipGain: 0.35, // насколько сильнее болтается каждая следующая кость
  maxAngle: 0.9,
};

export function createSpringChain(frame, bones, params = {}) {
  const p = { ...DEFAULT_SPRING, ...params };
  for (const bone of bones) bone.userData.posed ??= bone.rotation.clone();
  const s = {
    angle: bones.map(() => new THREE.Vector2()), // x — вперёд/назад, y — вбок
    velocity: bones.map(() => new THREE.Vector2()),
    prevPos: null,
    prevVel: new THREE.Vector3(),
    prevQuat: new THREE.Quaternion(),
    prevOmega: new THREE.Vector3(),
  };

  function write() {
    bones.forEach((bone, i) => {
      const posed = bone.userData.posed;
      bone.rotation.set(posed.x + s.angle[i].x, posed.y, posed.z + s.angle[i].y);
    });
  }

  function update(dt) {
    dt = Math.min(dt, 1 / 20); // после паузы вкладки не «выстреливаем»
    frame.getWorldPosition(_pos);
    frame.getWorldQuaternion(_q);
    if (!s.prevPos) {
      s.prevPos = _pos.clone();
      s.prevQuat.copy(_q);
      write();
      return;
    }
    const step = Math.max(dt, 1e-4);

    _vel.copy(_pos).sub(s.prevPos).divideScalar(step);
    _acc.copy(_vel).sub(s.prevVel).divideScalar(step);
    s.prevPos.copy(_pos);
    s.prevVel.copy(_vel);

    // Угловая скорость/ускорение в осях основания
    _dq.copy(s.prevQuat).invert().multiply(_q);
    if (_dq.w < 0) _dq.set(-_dq.x, -_dq.y, -_dq.z, -_dq.w);
    const half = Math.acos(Math.min(_dq.w, 1));
    const sinHalf = Math.sin(half);
    if (sinHalf > 1e-6) _omega.set(_dq.x, _dq.y, _dq.z).multiplyScalar((2 * half) / sinHalf / step);
    else _omega.set(0, 0, 0);
    _alpha.copy(_omega).sub(s.prevOmega).divideScalar(step).clampLength(0, 400);
    s.prevOmega.copy(_omega);
    s.prevQuat.copy(_q);

    // Линейное ускорение в осях основания (Y — вдоль цепочки)
    _acc.applyQuaternion(_qi.copy(_q).invert()).clampLength(0, 60);

    const steps = Math.ceil(dt / (1 / 120));
    const h = dt / steps;
    for (let n = 0; n < steps; n++) {
      for (let i = 0; i < bones.length; i++) {
        const a = s.angle[i];
        const v = s.velocity[i];
        const k = 1 + i * p.tipGain;
        // Кости отстают: ускорение по +Z отклоняет к −Z (поворот X в минус),
        // по +X — к −X (поворот Z в плюс); по +Y (вдоль) — сгиб назад
        let fx = -p.stiffness * a.x - p.damping * v.x
          - k * (p.inertia * _acc.z + p.turnInertia * _alpha.x + p.droop * _acc.y);
        let fy = -p.stiffness * a.y - p.damping * v.y + k * (p.inertia * _acc.x - p.turnInertia * _alpha.z);
        if (i > 0) {
          fx += p.coupling * (s.angle[i - 1].x - a.x);
          fy += p.coupling * (s.angle[i - 1].y - a.y);
        }
        v.x += fx * h;
        v.y += fy * h;
        a.x = THREE.MathUtils.clamp(a.x + v.x * h, -p.maxAngle, p.maxAngle);
        a.y = THREE.MathUtils.clamp(a.y + v.y * h, -p.maxAngle, p.maxAngle);
      }
    }
    write();
  }

  // Сбросить отклонения и «память» о движении (для повторяемого экспорта)
  function reset() {
    for (const v of [...s.angle, ...s.velocity]) v.set(0, 0);
    s.prevPos = null;
    s.prevVel.set(0, 0, 0);
    s.prevOmega.set(0, 0, 0);
    write();
  }

  return { update, write, reset, state: s };
}
