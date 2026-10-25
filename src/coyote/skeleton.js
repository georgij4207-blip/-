import * as THREE from 'three';
import { solveLegToFloor } from './legs.js';

// Единый скелет койота: реестр всех суставов с одинаковым интерфейсом.
//
// Каждый сустав — это одна или несколько групп/костей сцены и набор
// «каналов» (осей), которыми можно его крутить. Значение канала — смещение
// от позы покоя («стоит») в градусах (для поворотов) или сантиметрах
// (для смещений, 1 см = 0.01 единицы сцены). Поэтому поза — просто объект
// { id сустава: { канал: значение } }, а нулевая поза — стойка.
//
// Каналы левой и правой стороны зеркальны: одно и то же значение даёт
// симметричное движение (оси Y и Z у правой стороны меняют знак).
//
// Режим «стопы на полу»: бедро, колено и голеностоп ног подбираются
// обратной кинематикой, чтобы стопы стояли на своих точках на полу при
// любом наклоне и высоте таза. Ползунки этих суставов тогда только
// показывают результат.

const { degToRad, radToDeg } = THREE.MathUtils;

// --- Описание каналов ---

// Поворот вокруг оси. sign делает «+» направлением из правой части подписи.
const rot = (axis, label, min, max, sign = 1) => ({ key: axis, kind: 'rot', axis, label, min, max, sign, unit: '°' });
// Смещение вдоль оси, в сантиметрах
const move = (key, axis, label, min, max) => ({ key, kind: 'pos', axis, label, min, max, sign: 1, unit: ' см' });
// Параметр мимики: значение уходит в face.state[field]
const param = (key, field, label, min, max, unit) => ({ key, kind: 'fn', field, label, min, max, sign: 1, unit });

export const GROUPS = [
  ['body', 'Корпус'],
  ['head', 'Голова'],
  ['face', 'Лицо'],
  ['ears', 'Уши'],
  ['armL', 'Левая рука'],
  ['armR', 'Правая рука'],
  ['handL', 'Левая кисть'],
  ['handR', 'Правая кисть'],
  ['legL', 'Левая нога'],
  ['legR', 'Правая нога'],
  ['tail', 'Хвост'],
];

const SIDES = [
  ['L', 1, 'Левая', 'Левое', 'Левая'],
  ['R', -1, 'Правая', 'Правое', 'Правая'],
];

function buildJointList(coyote) {
  const { hips, neckPivot, headPivot, head } = coyote.userData.parts;
  const face = coyote.userData.face;
  const ears = coyote.userData.ears;
  const arms = coyote.userData.arms;
  const legs = coyote.userData.legs;
  const tail = coyote.userData.tail;
  const list = [];
  const add = (joint) => list.push({ mirror: 1, ...joint });

  // --- Корпус ---
  add({
    id: 'hips', group: 'body', label: 'Таз', objects: [hips],
    channels: [
      move('h', 'y', 'Высота: ниже ↔ выше', -120, 40),
      rot('x', 'Наклон: назад ↔ вперёд', -40, 60),
      rot('y', 'Поворот: вправо ↔ влево', -60, 60),
      rot('z', 'Крен: вправо ↔ влево', -30, 30, -1),
    ],
  });

  // --- Голова ---
  add({
    id: 'neck', group: 'head', label: 'Шея', objects: [neckPivot],
    channels: [
      rot('x', 'Наклон: назад ↔ вперёд', -40, 60),
      rot('y', 'Поворот: вправо ↔ влево', -60, 60),
      rot('z', 'Наклон вбок: вправо ↔ влево', -40, 40, -1),
    ],
  });
  add({
    id: 'head', group: 'head', label: 'Голова', objects: [headPivot],
    channels: [
      rot('x', 'Кивок: вверх ↔ вниз', -50, 60),
      rot('y', 'Поворот: вправо ↔ влево', -70, 70),
      rot('z', 'Наклон вбок: вправо ↔ влево', -40, 40, -1),
    ],
  });

  // --- Лицо ---
  for (const [s, side, , , fem] of SIDES) {
    add({
      id: `brow${s}`, group: 'face', label: `${fem} бровь`, mirror: side, pair: `brow${s === 'L' ? 'R' : 'L'}`,
      objects: [head.getObjectByName(`brow${s}`)],
      channels: [
        rot('z', 'Изгиб: грустно ↔ сердито', -40, 40),
        move('up', 'y', 'Опустить ↔ поднять', -8, 14),
      ],
    });
  }
  add({
    id: 'eyes', group: 'face', label: 'Взгляд', objects: [],
    channels: [
      param('x', 'lookX', 'Вниз ↔ вверх', -40, 40, '°'),
      param('y', 'lookY', 'Вправо ↔ влево', -45, 45, '°'),
      param('cross', 'cross', 'Врозь ↔ к носу (косоглазие)', -20, 40, '°'),
      param('pupil', 'pupil', 'Зрачки: меньше ↔ больше', -60, 60, '%'),
    ],
  });
  for (const [s, , , neut] of SIDES) {
    add({
      id: `lid${s}`, group: 'face', label: `${neut} веко`, pair: `lid${s === 'L' ? 'R' : 'L'}`, objects: [],
      channels: [param('close', `lid${s}`, 'Открыто ↔ закрыто', 0, 100, '%')],
    });
  }
  add({
    id: 'mouth', group: 'face', label: 'Рот', objects: [],
    channels: [
      param('open', 'jaw', 'Закрыт ↔ открыт', 0, 45, '°'),
      param('smileL', 'smileL', 'Левый уголок: вниз ↔ вверх', -100, 100, '%'),
      param('smileR', 'smileR', 'Правый уголок: вниз ↔ вверх', -100, 100, '%'),
    ],
  });
  add({
    id: 'fx', group: 'face', label: 'После взрыва', objects: [],
    channels: [
      param('soot', 'soot', 'Копоть', 0, 100, '%'),
      param('stars', 'stars', 'Звёздочки', 0, 100, '%'),
    ],
  });

  // --- Уши ---
  for (const [s, side, , neut] of SIDES) {
    const ear = side > 0 ? ears.left : ears.right;
    const { pivot, mid, tip } = ear.userData.parts;
    const pair = (id) => id.replace(`ear${s}`, `ear${s === 'L' ? 'R' : 'L'}`);
    add({
      id: `ear${s}`, group: 'ears', label: `${neut} ухо`, mirror: side, pair: pair(`ear${s}`), objects: [pivot],
      channels: [
        rot('x', 'Наклон: назад ↔ вперёд', -80, 40),
        rot('z', 'Развал: внутрь ↔ наружу', -30, 80, -1),
        rot('y', 'Поворот: внутрь ↔ наружу', -45, 60),
      ],
    });
    add({
      id: `ear${s}.mid`, group: 'ears', label: `${neut} ухо: середина`, mirror: side, pair: pair(`ear${s}.mid`), objects: [mid],
      channels: [rot('x', 'Изгиб: назад ↔ вперёд', -45, 70)],
    });
    add({
      id: `ear${s}.tip`, group: 'ears', label: `${neut} ухо: кончик`, mirror: side, pair: pair(`ear${s}.tip`), objects: [tip],
      channels: [rot('x', 'Изгиб: назад ↔ вперёд', -45, 80)],
    });
  }

  // --- Руки и кисти ---
  for (const [s, side] of SIDES) {
    const arm = side > 0 ? arms.left : arms.right;
    const j = arm.userData.joints;
    const other = s === 'L' ? 'R' : 'L';
    const g = `arm${s}`;
    const h = `hand${s}`;
    const base = { mirror: side };
    add({
      ...base, id: `${g}.shoulder`, group: g, label: 'Плечо', pair: `arm${other}.shoulder`, objects: [j.shoulder],
      channels: [
        rot('x', 'Подъём: назад ↔ вперёд', -60, 170, -1),
        rot('z', 'Отведение: к телу ↔ от тела', -10, 175),
        rot('y', 'Поворот: внутрь ↔ наружу', -90, 90),
      ],
    });
    add({
      ...base, id: `${g}.elbow`, group: g, label: 'Локоть', pair: `arm${other}.elbow`, objects: [j.elbow],
      channels: [rot('x', 'Разогнуть ↔ согнуть', -25, 125, -1)],
    });
    add({
      ...base, id: `${g}.wrist`, group: g, label: 'Запястье', pair: `arm${other}.wrist`, objects: [j.wrist],
      channels: [
        rot('z', 'Сгиб: назад ↔ к ладони', -60, 70, -1),
        rot('x', 'Вбок', -40, 40),
      ],
    });
    j.fingers.forEach((finger, i) => {
      add({
        ...base, id: `${h}.f${i}`, group: h, label: `Палец ${i + 1}: основание`, pair: `hand${other}.f${i}`, objects: [finger.base],
        channels: [rot('x', 'Разогнуть ↔ согнуть', -40, 90, -1)],
      });
      add({
        ...base, id: `${h}.f${i}.mid`, group: h, label: `Палец ${i + 1}: кончик`, pair: `hand${other}.f${i}.mid`, objects: [finger.mid],
        channels: [rot('x', 'Разогнуть ↔ согнуть', -30, 100, -1)],
      });
    });
    add({
      ...base, id: `${h}.thumb`, group: h, label: 'Большой палец: основание', pair: `hand${other}.thumb`, objects: [j.thumb.base],
      channels: [
        rot('x', 'Отвести ↔ к ладони', -40, 80, -1),
        rot('z', 'Вбок', -40, 40, -1),
      ],
    });
    add({
      ...base, id: `${h}.thumb.mid`, group: h, label: 'Большой палец: кончик', pair: `hand${other}.thumb.mid`, objects: [j.thumb.mid],
      channels: [rot('x', 'Разогнуть ↔ согнуть', -30, 90, -1)],
    });
  }

  // --- Ноги ---
  for (const [s, side] of SIDES) {
    const leg = side > 0 ? legs.left : legs.right;
    const j = leg.userData.joints;
    const other = s === 'L' ? 'R' : 'L';
    const g = `leg${s}`;
    const base = { mirror: side, leg };
    add({
      ...base, id: `${g}.hip`, group: g, label: 'Бедро', pair: `leg${other}.hip`, objects: [j.hip], ik: ['x'],
      channels: [
        rot('x', 'Назад ↔ вперёд', -70, 100, -1),
        rot('z', 'К центру ↔ в сторону', -20, 45),
      ],
    });
    add({
      ...base, id: `${g}.knee`, group: g, label: 'Колено', pair: `leg${other}.knee`, objects: [j.knee], ik: ['x'],
      channels: [rot('x', 'Разогнуть ↔ согнуть', -60, 100, -1)],
    });
    add({
      ...base, id: `${g}.ankle`, group: g, label: 'Голеностоп', pair: `leg${other}.ankle`, objects: [j.ankle], ik: ['x'],
      channels: [rot('x', 'Носок вниз ↔ вверх', -70, 70, -1)],
    });
    add({
      ...base, id: `${g}.toes`, group: g, label: 'Пальцы ног', pair: `leg${other}.toes`, objects: [j.toes],
      channels: [rot('x', 'Вниз ↔ вверх', -40, 80, -1)],
    });
    add({
      ...base, id: `${g}.step`, group: g, label: 'Шаг (когда стопы на полу)', pair: `leg${other}.step`, objects: [],
      channels: [{ key: 'step', kind: 'plant', label: 'Стопа назад ↔ вперёд', min: -80, max: 80, sign: 1, unit: ' см' }],
    });
  }

  // --- Хвост ---
  const { pivot, bones } = tail.userData.parts;
  add({
    id: 'tail', group: 'tail', label: 'Хвост: основание', objects: [pivot],
    channels: [
      rot('x', 'Ниже ↔ выше', -40, 110),
      rot('z', 'Вправо ↔ влево', -70, 70, -1),
    ],
  });
  bones.slice(1).forEach((bone, i) => {
    add({
      id: `tail.${i + 1}`, group: 'tail', label: `Хвост: сегмент ${i + 1}`, objects: [bone], posed: true,
      channels: [
        rot('x', 'Вниз ↔ вверх', -60, 60),
        rot('z', 'Вправо ↔ влево', -45, 45, -1),
      ],
    });
  });

  // Каналы мимики пишут в состояние лица
  for (const joint of list) for (const c of joint.channels) if (c.kind === 'fn') c.target = face.state;
  return list;
}

// Зеркало для правой стороны: у поворотов меняют знак оси Y и Z,
// у смещений — ось X
const mirrored = (joint, c) => ((c.kind === 'rot') === (c.axis === 'x') ? 1 : joint.mirror);

export function createSkeleton(coyote) {
  const joints = buildJointList(coyote);
  const byId = new Map(joints.map((j) => [j.id, j]));

  // Поза покоя каждого объекта — то, как он стоит сейчас (стойка)
  for (const joint of joints) {
    joint.values = Object.fromEntries(joint.channels.map((c) => [c.key, 0]));
    joint.rest = joint.objects.map((o) => ({
      rotation: (joint.posed ? o.userData.posed : o.rotation).clone(),
      position: o.position.clone(),
    }));
  }

  const state = { feetOnFloor: true, tween: null };
  const plant = new THREE.Vector3();

  function writeJoint(joint) {
    joint.objects.forEach((object, i) => {
      const rest = joint.rest[i];
      const rotation = joint.posed ? object.userData.posed : object.rotation;
      for (const c of joint.channels) {
        const v = joint.values[c.key] * c.sign * mirrored(joint, c);
        if (c.kind === 'rot') rotation[c.axis] = rest.rotation[c.axis] + degToRad(v);
        else if (c.kind === 'pos') object.position[c.axis] = rest.position[c.axis] + v * 0.01;
      }
    });
    for (const c of joint.channels) if (c.kind === 'fn') c.target[c.field] = joint.values[c.key];
  }

  // Подбор ног под пол и обратное чтение углов в ползунки
  function solveFeet() {
    coyote.updateMatrixWorld(true);
    for (const leg of [coyote.userData.legs.left, coyote.userData.legs.right]) {
      const s = leg.userData.side > 0 ? 'L' : 'R';
      plant.copy(leg.userData.plant);
      plant.z += byId.get(`leg${s}.step`).values.step * 0.01;
      coyote.localToWorld(plant);
      solveLegToFloor(leg, plant);
      for (const id of [`leg${s}.hip`, `leg${s}.knee`, `leg${s}.ankle`]) {
        const joint = byId.get(id);
        const c = joint.channels[0];
        joint.values.x = radToDeg(joint.objects[0].rotation.x - joint.rest[0].rotation.x) / c.sign;
      }
    }
  }

  function apply() {
    for (const joint of joints) writeJoint(joint);
    if (state.feetOnFloor) solveFeet();
  }

  // Текущая поза как { id: { канал: значение } } без нулей
  function getPose() {
    const pose = {};
    for (const joint of joints) {
      const nonZero = Object.entries(joint.values).filter(([, v]) => Math.abs(v) > 1e-3);
      if (nonZero.length) pose[joint.id] = Object.fromEntries(nonZero);
    }
    return pose;
  }

  // Плавный переход в позу: отсутствующие каналы уходят в ноль (стойку).
  // scope(joint) ограничивает, какие суставы трогать (например, только лицо):
  // остальные сохраняют текущие значения.
  function setPose(pose, duration = 0.45, scope = () => true) {
    const from = joints.map((j) => ({ ...j.values }));
    const to = joints.map((j) =>
      scope(j) || pose[j.id]
        ? Object.fromEntries(j.channels.map((c) => [c.key, pose[j.id]?.[c.key] ?? 0]))
        : { ...j.values }
    );
    if (duration <= 0) {
      joints.forEach((j, i) => Object.assign(j.values, to[i]));
      apply();
      return;
    }
    state.tween = { from, to, time: 0, duration };
  }

  function setValue(id, key, value) {
    const joint = byId.get(id);
    joint.values[key] = value;
    state.tween = null;
    apply();
  }

  // Вызывать каждый кадр: ведёт плавный переход между позами
  function update(dt) {
    const tw = state.tween;
    if (!tw) return false;
    tw.time = Math.min(tw.time + dt, tw.duration);
    const t = tw.time / tw.duration;
    const k = t < 0.5 ? 4 * t * t * t : 1 - Math.pow(-2 * t + 2, 3) / 2; // easeInOutCubic
    joints.forEach((joint, i) => {
      for (const key of Object.keys(joint.values)) {
        joint.values[key] = tw.from[i][key] + (tw.to[i][key] - tw.from[i][key]) * k;
      }
    });
    apply();
    if (t >= 1) state.tween = null;
    return true;
  }

  return {
    joints,
    get: (id) => byId.get(id),
    apply,
    getPose,
    setPose,
    setValue,
    update,
    get feetOnFloor() {
      return state.feetOnFloor;
    },
    set feetOnFloor(on) {
      state.feetOnFloor = on;
      apply();
    },
    isIkChannel: (joint, key) => state.feetOnFloor && !!joint.ik?.includes(key),
  };
}
