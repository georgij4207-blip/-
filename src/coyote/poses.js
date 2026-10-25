// Готовые позы в формате единого скелета: { id сустава: { канал: значение } }.
// Значения — смещения от стойки: градусы для поворотов, сантиметры для
// смещений. Всё, что не указано, остаётся как в стойке. Позы рассчитаны на
// режим «стопы на полу»: углы ног подбирает IK, позы задают только высоту
// и наклон таза и шаг стоп.

// Одинаковые значения для левой и правой стороны (они зеркальны)
function both(id, values) {
  return { [id.replace('*', 'L')]: values, [id.replace('*', 'R')]: values };
}

// Сжать все пальцы кисти: curl — основания, tip — кончики
function fist(side, curl, tip = curl, thumb = curl * 0.8) {
  const h = `hand${side}`;
  return {
    [`${h}.f0`]: { x: curl }, [`${h}.f1`]: { x: curl }, [`${h}.f2`]: { x: curl },
    [`${h}.f0.mid`]: { x: tip }, [`${h}.f1.mid`]: { x: tip }, [`${h}.f2.mid`]: { x: tip },
    [`${h}.thumb`]: { x: thumb }, [`${h}.thumb.mid`]: { x: thumb },
  };
}

// Изогнуть хвост: одинаковый изгиб каждого сегмента
function tailCurve(x, z = 0) {
  const pose = {};
  for (let i = 1; i <= 6; i++) pose[`tail.${i}`] = { x, z };
  return pose;
}

export const POSES = {
  stand: {
    label: 'Стоит',
    pose: {},
  },

  sneak: {
    label: 'Крадётся',
    pose: {
      hips: { h: -50, x: 28 },
      neck: { x: 15 },
      head: { x: -30, y: 12 },
      eyes: { y: 25 },
      ...both('brow*', { z: 18, up: -2 }),
      ...both('ear*', { x: -35, z: 10 }),
      // Лапы подняты перед грудью и свисают кистями — «на цыпочках»
      ...both('arm*.shoulder', { x: 45, z: 5 }),
      ...both('arm*.elbow', { x: 85 }),
      ...both('arm*.wrist', { z: 55 }),
      ...fist('L', 25, 30, 10),
      ...fist('R', 25, 30, 10),
      'legL.step': { step: 40 },
      'legR.step': { step: -30 },
      ...both('leg*.toes', { x: -10 }),
      tail: { x: -25 },
      ...tailCurve(-6),
    },
  },

  scared: {
    label: 'Испуг',
    pose: {
      hips: { h: 18, x: -10 },
      neck: { x: -10 },
      head: { x: 12 },
      eyes: { x: 10 },
      ...both('brow*', { z: -30, up: 12 }),
      // Уши торчком, шерсть дыбом — хвост тоже
      ...both('ear*', { x: 20, z: -20 }),
      ...both('ear*.mid', { x: -10 }),
      ...both('ear*.tip', { x: -10 }),
      ...both('arm*.shoulder', { x: 15, z: 125, y: 30 }),
      ...both('arm*.elbow', { x: 20 }),
      ...both('arm*.wrist', { z: -35 }),
      ...fist('L', -25, -15, -30),
      ...fist('R', -25, -15, -30),
      'legL.step': { step: 10 },
      'legR.step': { step: -10 },
      ...both('leg*.toes', { x: 25 }),
      tail: { x: 95 },
      ...tailCurve(-7),
    },
  },

  triumph: {
    label: 'Торжество',
    pose: {
      hips: { x: -8, y: -10 },
      neck: { x: -4 },
      head: { x: -12, z: 8 },
      eyes: { x: 8 },
      ...both('brow*', { z: 15, up: 4 }),
      ...both('ear*', { x: 15, z: -10 }),
      // Правый кулак вскинут: плечо в сторону, рука развёрнута так, что
      // локоть сгибается вверх (мимо большой головы); левая упёрта в бок
      'armR.shoulder': { x: 5, z: 88, y: 90 },
      'armR.elbow': { x: 70 },
      ...fist('R', 90, 95, 70),
      'armL.shoulder': { x: -25, z: 45, y: -30 },
      'armL.elbow': { x: 95 },
      'armL.wrist': { z: 40 },
      ...fist('L', 40, 40, 20),
      'legL.step': { step: 25 },
      'legR.step': { step: -15 },
      tail: { x: 45, z: 20 },
      ...tailCurve(10, 4),
    },
  },

  disappointed: {
    label: 'Разочарование',
    pose: {
      hips: { h: -12, x: 12 },
      neck: { x: 18 },
      head: { x: 12, z: -8 },
      eyes: { x: -25 },
      ...both('brow*', { z: -28, up: -3 }),
      // Уши обвисли в стороны, руки висят плетью
      ...both('ear*', { x: -35, z: 55 }),
      ...both('ear*.mid', { x: 35 }),
      ...both('ear*.tip', { x: 45 }),
      ...both('arm*.shoulder', { x: -8, z: -8 }),
      ...both('arm*.elbow', { x: -20 }),
      ...fist('L', -10, 0, 0),
      ...fist('R', -10, 0, 0),
      'legL.step': { step: 5 },
      'legR.step': { step: -5 },
      tail: { x: -35 },
      ...tailCurve(-9),
    },
  },
};

// Эмоции — позы лица и ушей. Применяются поверх позы тела: меняют только
// суставы групп «Лицо» и «Уши», тело и голова остаются как были.
export const EMOTION_SCOPE = (joint) => joint.group === 'face' || joint.group === 'ears';

export const EMOTIONS = {
  neutral: {
    label: 'Спокойно',
    pose: {},
  },

  sly: {
    label: 'Хитрая улыбка',
    pose: {
      // Прищур, взгляд искоса, одна бровь заломлена — и кривая ухмылка
      lidL: { close: 48 },
      lidR: { close: 40 },
      eyes: { x: -4, y: 22, pupil: -10 },
      browL: { z: 22, up: 2 },
      browR: { z: -8, up: 6 },
      mouth: { open: 0, smileL: 100, smileR: 45 },
      ...both('ear*', { x: -12, y: 15 }),
    },
  },

  fear: {
    label: 'Испуг',
    pose: {
      // Глаза на выкате, зрачки-точки, брови домиком, рот разинут
      eyes: { x: 4, pupil: -55 },
      browL: { z: -35, up: 14 },
      browR: { z: -35, up: 14 },
      mouth: { open: 30, smileL: -70, smileR: -70 },
      ...both('ear*', { x: -60, z: 15 }),
    },
  },

  anger: {
    label: 'Злость',
    pose: {
      // Брови сведены, взгляд исподлобья, оскал, уши прижаты
      lidL: { close: 42 },
      lidR: { close: 42 },
      eyes: { x: 12, pupil: -20 },
      browL: { z: 22, up: -1 },
      browR: { z: 22, up: -1 },
      mouth: { open: 14, smileL: -100, smileR: -100 },
      ...both('ear*', { x: -45, z: 25 }),
    },
  },

  gloom: {
    label: 'Уныние',
    pose: {
      // Веки тяжёлые, взгляд в пол, уши повисли
      lidL: { close: 58 },
      lidR: { close: 58 },
      eyes: { x: -22 },
      browL: { z: -30, up: -2 },
      browR: { z: -30, up: -2 },
      mouth: { smileL: -75, smileR: -75 },
      ...both('ear*', { x: -30, z: 55 }),
      ...both('ear*.mid', { x: 35 }),
      ...both('ear*.tip', { x: 45 }),
    },
  },

  stunned: {
    label: 'Ошарашен после взрыва',
    pose: {
      // Весь в копоти, глаза в кучу и вразнобой, вокруг головы звёздочки
      fx: { soot: 100, stars: 100 },
      lidL: { close: 30 },
      lidR: { close: 55 },
      eyes: { cross: 28, pupil: -25 },
      browL: { z: -20, up: 12 },
      browR: { z: 18, up: -5 },
      mouth: { open: 18, smileL: -20, smileR: -55 },
      // Уши: одно обвисло, другое опалённым торчком
      earL: { x: -20, z: 60 },
      'earL.mid': { x: 40 },
      'earL.tip': { x: 55 },
      earR: { x: 10, z: -15 },
      'earR.mid': { x: -25 },
      'earR.tip': { x: -30 },
    },
  },
};
