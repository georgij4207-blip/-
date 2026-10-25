import * as THREE from 'three';
import { POSES } from './poses.js';

// Анимации койота по 12 принципам анимации:
//  · сжатие и растяжение — масштаб всего койота от стоп с сохранением объёма
//    (root.sq: + вытянуть, − сплющить);
//  · подготовка (предвкушение) — присед и замах перед прыжком и испугом;
//  · захлёст и запаздывание — уши и хвост на пружинах (spring.js) догоняют
//    тело сами, плюс отстающие голова и руки в ключах;
//  · смягчение начала и конца — ключи с плавными кривыми, «out» на взлёте
//    и «in» на падении дают дугу броска;
//  · дуги, вторичное действие (руки, хвост, мимика), тайминг, утрирование,
//    выразительная поза (стейджинг) и твёрдый рисунок — в самих ключах.
//
// Клип — либо цикл (ходьба, бег: поза = функция фазы), либо ключи
// { t, ease, pose, root: { y, sq } }. Поза — в формате единого скелета
// (смещения от стойки). Каналы, которых в клипе нет, уходят в стойку,
// а мимика, которой нет, остаётся как есть — эмоцию можно менять на ходу.

const EASE = {
  linear: (t) => t,
  in: (t) => t * t, // разгон (падение)
  out: (t) => 1 - (1 - t) * (1 - t), // торможение (взлёт, удар)
  inout: (t) => (t < 0.5 ? 2 * t * t : 1 - (-2 * t + 2) ** 2 / 2),
};
const smooth = (t) => t * t * (3 - 2 * t);

// Одинаковые значения для левой и правой стороны
function both(id, values) {
  return { [id.replace('*', 'L')]: values, [id.replace('*', 'R')]: values };
}

// Стопа на цикле шага. phase: 0 — стопа впереди (удар пяткой), π — сзади
// (толчок носком). Опора (стопа на полу) — когда стопа едет назад.
function footCycle(phase, stride, lift, heel = 15, push = 30) {
  const c = Math.cos(phase);
  const swing = Math.max(0, -Math.sin(phase)); // перенос: стопа едет вперёд в воздухе
  return {
    step: stride * c,
    lift: lift * swing * swing,
    roll: c > 0 ? heel * c : push * c * (1 - swing * 0.5),
  };
}

// --- Циклы ---

const WALK_PERIOD = 1.1;
const RUN_PERIOD = 0.6;

function walkPose(t) {
  const T = WALK_PERIOD;
  const phi = (t / T) * Math.PI * 2;
  const L = footCycle(phi, 30, 26);
  const R = footCycle(phi + Math.PI, 30, 26);
  const c2 = Math.cos(2 * phi);
  return {
    pose: {
      hips: { h: -9 - 5 * c2, x: 8, y: 7 * Math.cos(phi), z: 3 * Math.sin(phi) },
      neck: { x: 2 * c2, y: -4 * Math.cos(phi) },
      head: { x: -6 + 3 * c2 },
      'legL.step': L, 'legR.step': R,
      'legL.toes': { x: Math.max(0, -L.roll) * 0.9 }, 'legR.toes': { x: Math.max(0, -R.roll) * 0.9 },
      // Руки — в противофазе ногам, предплечье запаздывает за плечом
      'armL.shoulder': { x: -28 * Math.cos(phi), z: 4 },
      'armR.shoulder': { x: 28 * Math.cos(phi), z: 4 },
      'armL.elbow': { x: 18 - 14 * Math.cos(phi - 0.6) },
      'armR.elbow': { x: 18 + 14 * Math.cos(phi - 0.6) },
      ...both('arm*.wrist', { z: 10 }),
      tail: { x: 5, z: 8 * Math.sin(phi) },
    },
    root: { y: 0, sq: -0.025 * c2 }, // чуть сжимается при ударе пяткой
  };
}

function runPose(t) {
  const T = RUN_PERIOD;
  const phi = (t / T) * Math.PI * 2;
  const L = footCycle(phi, 52, 50, 10, 45);
  const R = footCycle(phi + Math.PI, 52, 50, 10, 45);
  const flight = Math.abs(Math.sin(2 * phi)); // 0 — касание, 1 — полёт
  return {
    pose: {
      hips: { h: -16 - 6 * Math.cos(2 * phi), x: 24, y: 10 * Math.cos(phi) },
      neck: { x: 5 },
      head: { x: -26 + 4 * Math.cos(2 * phi) }, // голова смотрит вперёд, тело в наклоне
      eyes: { x: 6 },
      'legL.step': L, 'legR.step': R,
      'legL.toes': { x: Math.max(0, -L.roll) }, 'legR.toes': { x: Math.max(0, -R.roll) },
      'armL.shoulder': { x: -55 * Math.cos(phi), z: 8 },
      'armR.shoulder': { x: 55 * Math.cos(phi), z: 8 },
      ...both('arm*.elbow', { x: 90 }),
      ...both('hand*.f0', { x: 60 }), ...both('hand*.f1', { x: 60 }), ...both('hand*.f2', { x: 60 }),
      ...both('ear*', { x: -30, z: 8 }),
      tail: { x: 25, z: 10 * Math.sin(phi) },
    },
    // Полёт между шагами: подпрыгивает и вытягивается, на касании — сжатие
    root: { y: 0.16 * flight, sq: 0.06 * flight - 0.08 * (1 - flight) ** 4 },
  };
}

// --- Ключевые клипы ---

const CROUCH = { hips: { h: -55, x: 30 }, neck: { x: 10 }, head: { x: -25 } };

const FEAR_FACE = {
  eyes: { pupil: -60, x: 4 },
  browL: { z: -35, up: 14 }, browR: { z: -35, up: 14 },
  mouth: { open: 32, smileL: -80, smileR: -80 },
};

const STUNNED_FACE = {
  fx: { stars: 100 },
  eyes: { cross: 26, pupil: -25 },
  lidL: { close: 35 }, lidR: { close: 55 },
  browL: { z: -20, up: 12 }, browR: { z: 18, up: -5 },
  mouth: { open: 16, smileL: -20, smileR: -55 },
};

const KEYED = {
  jump: {
    label: 'Прыжок',
    end: 'stop',
    keys: [
      // Подготовка: присед, руки назад, сжатие
      { t: 0.32, ease: 'inout', root: { sq: -0.14 }, pose: {
        ...CROUCH,
        ...both('arm*.shoulder', { x: -45, z: 15 }), ...both('arm*.elbow', { x: 30 }),
        browL: { z: 15 }, browR: { z: 15 }, lidL: { close: 20 }, lidR: { close: 20 }, mouth: { smileL: 40, smileR: 40 },
        tail: { x: -20 },
      } },
      // Толчок: тело вытягивается, руки вверх
      { t: 0.45, ease: 'out', root: { y: 0.35, sq: 0.2 }, pose: {
        hips: { h: 25, x: -4 }, head: { x: -12 },
        ...both('arm*.shoulder', { x: 160, z: 10 }), ...both('arm*.elbow', { x: 10 }),
        ...both('leg*.step', { roll: -40 }), ...both('leg*.toes', { x: 40 }),
        mouth: { smileL: 60, smileR: 60 },
      } },
      // Верхняя точка: ноги поджаты, форма вернулась
      { t: 0.82, ease: 'out', root: { y: 2.3, sq: 0 }, pose: {
        hips: { x: 10 }, head: { x: -6 },
        ...both('leg*.step', { step: 15, lift: 55, roll: -15 }),
        ...both('arm*.shoulder', { x: 120, z: 40 }), ...both('arm*.elbow', { x: 40 }),
        mouth: { open: 10, smileL: 70, smileR: 70 }, tail: { x: 30 },
      } },
      // Перед приземлением: ноги навстречу полу, вытягивается в падении
      { t: 1.16, ease: 'in', root: { y: 0.12, sq: 0.12 }, pose: {
        hips: { x: 4 },
        'legL.step': { step: 18, roll: 15 }, 'legR.step': { step: -8, roll: 10 },
        ...both('arm*.shoulder', { x: 80, z: 65 }), ...both('arm*.elbow', { x: 20 }),
        tail: { x: 45 },
      } },
      // Удар: сжатие и глубокий присед
      { t: 1.26, ease: 'out', root: { y: 0, sq: -0.24 }, pose: {
        hips: { h: -60, x: 28 }, neck: { x: 12 }, head: { x: -10 },
        'legL.step': { step: 18 }, 'legR.step': { step: -8 },
        ...both('arm*.shoulder', { x: 35, z: 60 }), ...both('arm*.elbow', { x: 45 }),
        tail: { x: -10 },
      } },
      // Выход в стойку с небольшим перелётом вверх
      { t: 1.55, ease: 'out', root: { sq: 0.04 }, pose: { hips: { h: 4 } } },
      { t: 1.8, ease: 'inout', root: { sq: 0 }, pose: {} },
    ],
  },

  hang: {
    label: 'Зависание',
    end: 'hold',
    keys: [
      { t: 0.2, ease: 'inout', root: { sq: -0.1 }, pose: { hips: { h: -30, x: 20 }, ...both('arm*.shoulder', { x: -30 }) } },
      // Выбегает в воздух — ноги ещё «бегут»
      { t: 0.55, ease: 'out', root: { y: 1.6, sq: 0.1 }, pose: {
        hips: { x: 22 }, head: { x: -22 },
        'legL.step': { step: 40, lift: 35, roll: 10 }, 'legR.step': { step: -40, lift: 15, roll: -40 },
        'armL.shoulder': { x: -45 }, 'armR.shoulder': { x: 60 }, ...both('arm*.elbow', { x: 90 }),
        ...both('ear*', { x: -30 }), tail: { x: 30 },
      } },
      { t: 0.95, ease: 'out', root: { y: 1.8, sq: 0 }, pose: {
        hips: { x: 22 }, head: { x: -22 },
        'legL.step': { step: -35, lift: 15, roll: -40 }, 'legR.step': { step: 40, lift: 35, roll: 10 },
        'armL.shoulder': { x: 60 }, 'armR.shoulder': { x: -45 }, ...both('arm*.elbow', { x: 90 }),
        ...both('ear*', { x: -30 }), tail: { x: 30 },
      } },
      // Замер. Смотрит вниз...
      { t: 1.5, ease: 'inout', root: { y: 1.8 }, pose: {
        hips: { x: 10 }, neck: { x: 25 }, head: { x: 40 }, eyes: { x: -35 },
        ...both('leg*.step', { lift: 10, roll: -30 }),
        ...both('arm*.shoulder', { x: 20, z: 15 }), ...both('arm*.elbow', { x: 30 }),
        ...both('ear*', { x: 10 }),
      } },
      // ...потом в камеру: «ой»
      { t: 2.1, ease: 'out', root: { y: 1.8 }, pose: {
        head: { x: -4 }, eyes: { pupil: -45 },
        browL: { z: -30, up: 10 }, browR: { z: -30, up: 10 },
        mouth: { smileL: -45, smileR: -45 }, lidL: { close: 15 }, lidR: { close: 15 },
        ...both('leg*.step', { roll: -45 }), ...both('leg*.toes', { x: -20 }),
        ...both('arm*.shoulder', { x: 5, z: 18 }), ...both('arm*.elbow', { x: 10 }),
        ...both('ear*', { x: -10, z: 40 }), ...both('ear*.tip', { x: 50 }),
        tail: { x: -30 },
      } },
      // Маленькая «табличка»: машет на прощание
      { t: 2.6, ease: 'inout', root: { y: 1.8 }, pose: {
        head: { x: -4, z: 8 }, eyes: { pupil: -45 },
        browL: { z: -30, up: 10 }, browR: { z: -30, up: 10 },
        mouth: { smileL: -45, smileR: -45 }, lidL: { close: 25 }, lidR: { close: 25 },
        ...both('leg*.step', { roll: -45 }), ...both('leg*.toes', { x: -20 }),
        'armL.shoulder': { x: 5, z: 18 }, 'armL.elbow': { x: 10 },
        'armR.shoulder': { z: 88, y: 90 }, 'armR.elbow': { x: 80 }, 'armR.wrist': { x: 20 },
        ...both('ear*', { x: -10, z: 40 }), ...both('ear*.tip', { x: 50 }),
        tail: { x: -30 },
      } },
    ],
  },

  fall: {
    label: 'Падение',
    end: 'hold',
    startHeight: 5, // если стоит на земле — падает с этой высоты
    keys: [
      // Миг перед падением: чуть подскакивает и сжимается
      { t: 0.14, ease: 'out', root: { y: 'start+0.15', sq: -0.08 }, pose: {
        ...FEAR_FACE, ...both('ear*', { x: 15 }), ...both('arm*.shoulder', { x: 40, z: 40 }),
      } },
      // Полёт вниз: вытянут в струну, руки и уши вверх
      { t: 0.8, ease: 'in', root: { y: 0.35, sq: 0.35 }, pose: {
        ...FEAR_FACE,
        ...both('arm*.shoulder', { x: 170, z: 15 }), ...both('arm*.elbow', { x: 0 }),
        ...both('ear*', { x: 10, z: -20 }), ...both('ear*.mid', { x: -30 }), ...both('ear*.tip', { x: -35 }),
        ...both('leg*.step', { lift: 25, roll: -40 }),
        tail: { x: 100 },
      } },
      // Удар: блин
      { t: 0.86, ease: 'out', root: { y: 0, sq: -0.62 }, pose: {
        hips: { h: -80, x: 15 },
        ...both('arm*.shoulder', { x: 30, z: 95 }), ...both('arm*.elbow', { x: 5 }),
        ...both('ear*', { z: 70 }), tail: { x: -30 },
        lidL: { close: 100 }, lidR: { close: 100 }, mouth: { open: 10 },
      } },
      { t: 1.15, ease: 'linear', root: { y: 0, sq: -0.58 }, pose: {
        hips: { h: -80, x: 15 },
        ...both('arm*.shoulder', { x: 30, z: 95 }), ...both('ear*', { z: 70 }), tail: { x: -30 },
        lidL: { close: 100 }, lidR: { close: 100 },
      } },
      // Выпрямляется пружиной — с перелётом
      { t: 1.45, ease: 'out', root: { sq: 0.12 }, pose: { hips: { h: 6 }, ...STUNNED_FACE } },
      // Стоит, ошарашенный, со звёздочками
      { t: 1.9, ease: 'inout', root: { sq: 0 }, pose: {
        hips: { x: 6, z: 4 }, head: { z: -10 },
        ...both('arm*.shoulder', { z: 6 }), ...both('arm*.elbow', { x: 5 }),
        ...STUNNED_FACE,
        earL: { x: -20, z: 60 }, 'earL.tip': { x: 50 }, earR: { x: 10, z: -10 },
        tail: { x: -25 },
      } },
    ],
  },

  scare: {
    label: 'Испуг',
    end: 'hold',
    keys: [
      // Подготовка: вздрогнул и сжался
      { t: 0.12, ease: 'inout', root: { sq: -0.1 }, pose: {
        hips: { h: -22, x: 10 }, head: { x: 15 }, lidL: { close: 35 }, lidR: { close: 35 },
        ...both('ear*', { x: -20 }), ...both('arm*.shoulder', { x: 20, z: -5 }), ...both('arm*.elbow', { x: 60 }),
      } },
      // Подскок: вытянулся, руки взлетели, уши и хвост торчком
      { t: 0.32, ease: 'out', root: { y: 1.4, sq: 0.3 }, pose: {
        hips: { h: 18, x: -10 }, neck: { x: -12 }, head: { x: 6 },
        ...FEAR_FACE,
        ...both('arm*.shoulder', { x: 60, z: 140, y: 20 }), ...both('arm*.elbow', { x: 20 }),
        ...both('hand*.f0', { x: -25 }), ...both('hand*.f1', { x: -25 }), ...both('hand*.f2', { x: -25 }),
        ...both('ear*', { x: 15, z: -20 }), ...both('ear*.mid', { x: -25 }), ...both('ear*.tip', { x: -30 }),
        ...both('leg*.step', { lift: 30, roll: -50 }), ...both('leg*.toes', { x: 50 }),
        tail: { x: 100 }, ...Object.fromEntries([1, 2, 3, 4, 5, 6].map((i) => [`tail.${i}`, { x: -7 }])),
      } },
      { t: 0.56, ease: 'in', root: { y: 0.04, sq: 0.1 }, pose: {
        ...FEAR_FACE,
        ...both('arm*.shoulder', { x: 50, z: 120, y: 20 }), ...both('arm*.elbow', { x: 25 }),
        ...both('ear*', { x: 15, z: -20 }), tail: { x: 100 },
        ...both('leg*.step', { lift: 5 }),
      } },
      // Приземлился, сжался
      { t: 0.66, ease: 'out', root: { y: 0, sq: -0.2 }, pose: {
        hips: { h: -30, x: 5 }, ...FEAR_FACE,
        ...both('arm*.shoulder', { x: 40, z: 110 }), ...both('arm*.elbow', { x: 35 }),
        ...both('ear*', { x: 10, z: -15 }), tail: { x: 95 },
      } },
      // Застыл в позе испуга
      { t: 1.25, ease: 'inout', root: { sq: 0 }, pose: { ...POSES.scared.pose, ...FEAR_FACE } },
    ],
  },
};

const CYCLES = {
  walk: { label: 'Ходьба', sample: walkPose, period: WALK_PERIOD },
  run: { label: 'Бег', sample: runPose, period: RUN_PERIOD },
};

// Длительность клипа (для циклов — один период) и зациклен ли он
export function clipInfo(name) {
  if (CYCLES[name]) return { duration: CYCLES[name].period, loop: true };
  const keys = KEYED[name].keys;
  return { duration: keys[keys.length - 1].t, loop: false };
}

export const ANIMATIONS = [
  ['walk', 'Ходьба'],
  ['run', 'Бег'],
  ['jump', 'Прыжок'],
  ['hang', 'Зависание'],
  ['fall', 'Падение'],
  ['scare', 'Испуг'],
];

// --- Плеер ---

const BLEND_IN = 0.25; // секунды плавного входа в клип из текущей позы

export function createAnimator(coyote) {
  const skeleton = coyote.userData.skeleton;
  const channels = [];
  for (const joint of skeleton.joints) {
    for (const c of joint.channels) channels.push({ joint, key: c.key, face: joint.group === 'face' });
  }

  let clip = null; // { name, kind, time, from, ... }
  const root = { y: 0, sq: 0 };
  const listeners = new Set();

  // Плоская поза { 'id|канал': значение }
  function flatten(pose) {
    const out = {};
    for (const [id, values] of Object.entries(pose)) {
      for (const [k, v] of Object.entries(values)) out[`${id}|${k}`] = v;
    }
    return out;
  }

  function currentFlat() {
    const out = {};
    for (const { joint, key } of channels) out[`${joint.id}|${key}`] = joint.values[key];
    return out;
  }

  function resolveRoot(value) {
    if (typeof value === 'string' && value.startsWith('start')) return clip.startY + Number(value.slice(5) || 0);
    return value;
  }

  // Значение ключа: заданное, иначе стойка (0), а мимика — как сейчас
  function keyValue(flat, ch, liveValue) {
    const v = flat[ch.id];
    if (v !== undefined) return v;
    return ch.face ? liveValue : 0;
  }

  function sampleKeyed(t) {
    const keys = clip.keys;
    let i = keys.findIndex((k) => k.t >= t);
    if (i === -1) i = keys.length - 1;
    const b = keys[i];
    const a = i === 0 ? clip.startKey : keys[i - 1];
    const span = b.t - a.t;
    const k = span > 0 ? EASE[b.ease ?? 'inout'](THREE.MathUtils.clamp((t - a.t) / span, 0, 1)) : 1;
    return { a, b, k };
  }

  function play(name) {
    const cycle = CYCLES[name];
    const keyed = KEYED[name];
    if (!cycle && !keyed) return;
    skeleton.feetOnFloor = true;
    const start = currentFlat();
    clip = {
      name,
      time: 0,
      cycle,
      start,
      startRoot: { y: coyote.position.y, sq: root.sq },
      end: keyed?.end ?? 'loop',
    };
    if (keyed) {
      clip.startY = coyote.position.y > 0.5 ? coyote.position.y : keyed.startHeight ?? 0;
      // В падении с земли — сначала оказываемся наверху
      if (keyed.startHeight && coyote.position.y <= 0.5) clip.startRoot.y = keyed.startHeight;
      clip.keys = keyed.keys.map((key) => ({ ...key, flat: flatten(key.pose ?? {}) }));
      clip.startKey = { t: 0, flat: start, root: { ...clip.startRoot }, isStart: true };
      clip.duration = keyed.keys[keyed.keys.length - 1].t;
    }
    listeners.forEach((fn) => fn(name));
  }

  // Плавно вернуться в стойку (мимика остаётся)
  function stop() {
    const start = currentFlat();
    clip = {
      name: 'stop',
      time: 0,
      start,
      startRoot: { y: coyote.position.y, sq: root.sq },
      end: 'done',
      keys: [{ t: 0.45, ease: 'inout', flat: {}, root: { y: 0, sq: 0 } }],
      duration: 0.45,
    };
    clip.startKey = { t: 0, flat: start, root: { ...clip.startRoot }, isStart: true };
    listeners.forEach((fn) => fn(null));
  }

  // Прервать без возврата (например, нажата поза на панели)
  function cancel() {
    clip = null;
    root.y = 0;
    root.sq = 0;
    coyote.position.y = 0;
    coyote.scale.set(1, 1, 1);
    listeners.forEach((fn) => fn(null));
  }

  function update(dt) {
    if (!clip) return false;
    clip.time += dt;
    const t = clip.time;

    const pose = {};
    let rootY;
    let rootSq;

    if (clip.cycle) {
      const s = clip.cycle.sample(t);
      const flat = flatten(s.pose);
      const w = smooth(Math.min(1, t / BLEND_IN));
      for (const ch of channels) {
        const id = `${ch.joint.id}|${ch.key}`;
        const live = ch.joint.values[ch.key];
        const target = flat[id] !== undefined ? flat[id] : ch.face ? live : 0;
        (pose[ch.joint.id] ??= {})[ch.key] = clip.start[id] + (target - clip.start[id]) * w;
      }
      rootY = clip.startRoot.y + ((s.root.y ?? 0) - clip.startRoot.y) * w;
      rootSq = clip.startRoot.sq + ((s.root.sq ?? 0) - clip.startRoot.sq) * w;
    } else {
      const tt = Math.min(t, clip.duration);
      const { a, b, k } = sampleKeyed(tt);
      for (const ch of channels) {
        const id = `${ch.joint.id}|${ch.key}`;
        const live = ch.joint.values[ch.key];
        const va = a.isStart ? a.flat[id] : keyValue(a.flat, { id, face: ch.face }, live);
        const vb = keyValue(b.flat, { id, face: ch.face }, live);
        (pose[ch.joint.id] ??= {})[ch.key] = va + (vb - va) * k;
      }
      const ra = a.root ?? {};
      const rb = b.root ?? {};
      const ya = resolveRoot(ra.y ?? (a.isStart ? clip.startRoot.y : prevRoot(a, 'y')));
      const yb = resolveRoot(rb.y ?? prevRoot(b, 'y'));
      const sa = ra.sq ?? prevRoot(a, 'sq');
      const sb = rb.sq ?? prevRoot(b, 'sq');
      rootY = ya + (yb - ya) * k;
      rootSq = sa + (sb - sa) * k;
    }

    root.y = rootY;
    root.sq = rootSq;
    coyote.position.y = rootY;
    // Сжатие/растяжение с сохранением объёма, опора — стопы
    const sy = Math.max(0.2, 1 + rootSq);
    const sxz = 1 / Math.sqrt(sy);
    coyote.scale.set(sxz, sy, sxz);
    skeleton.setPose(pose, 0);

    if (!clip.cycle && t >= clip.duration) {
      if (clip.end === 'stop') stop();
      else if (clip.end === 'done') {
        clip = null;
      } else if (clip.end === 'hold') {
        clip.time = clip.duration; // держим последнюю позу
      }
    }
    return true;
  }

  // Корень (y, sq) ключа, если в нём не задан: берём из ближайшего раньше
  function prevRoot(key, field) {
    if (key.isStart) return clip.startRoot[field];
    const keys = clip.keys;
    for (let i = keys.indexOf(key); i >= 0; i--) {
      const v = keys[i].root?.[field];
      if (v !== undefined) return field === 'y' ? resolveRoot(v) : v;
    }
    return clip.startRoot[field];
  }

  return {
    play,
    stop,
    cancel,
    update,
    get playing() {
      return clip?.name ?? null;
    },
    get rootHeight() {
      return root.y;
    },
    onChange: (fn) => listeners.add(fn),
  };
}
