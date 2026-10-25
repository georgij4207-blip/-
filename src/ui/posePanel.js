import * as THREE from 'three';

const { degToRad, radToDeg } = THREE.MathUtils;

/**
 * Панель позы: выбор цели (оба / левое / правое) и ползунки в градусах.
 * Ползунки — <input data-param="имя поля позы">, кнопки выбора — [data-target].
 * «Левое» — левая сторона самого койота (справа, если смотреть анфас).
 *
 * @param {HTMLElement} panel
 * @param {object} options
 * @param {{left: THREE.Object3D, right: THREE.Object3D}} options.parts — у каждой userData.pose
 * @param {(part, pose) => void} options.apply — применяет поле позы к части
 * @param {object} options.defaults — поза для кнопки «Сброс»
 * @param {object} [options.presets] — готовые позы для кнопок [data-preset]:
 *   { имя: { left: поза, right: поза } }
 */
export function setupPosePanel(panel, { parts, apply, defaults, presets = {} }) {
  const { left, right } = parts;
  const targets = { both: [left, right], left: [left], right: [right] };
  let selected = 'both';

  const segButtons = panel.querySelectorAll('[data-target]');
  const sliders = panel.querySelectorAll('input[data-param]');

  function show(slider, deg) {
    slider.value = deg;
    slider.parentElement.querySelector('output').textContent = `${Math.round(deg)}°`;
  }

  // Ползунки показывают позу первой из выбранных частей
  function syncSliders() {
    const pose = targets[selected][0].userData.pose;
    sliders.forEach((s) => show(s, radToDeg(pose[s.dataset.param])));
  }

  sliders.forEach((slider) => {
    slider.addEventListener('input', () => {
      const deg = Number(slider.value);
      show(slider, deg);
      for (const part of targets[selected]) apply(part, { [slider.dataset.param]: degToRad(deg) });
    });
  });

  segButtons.forEach((button) => {
    button.addEventListener('click', () => {
      selected = button.dataset.target;
      segButtons.forEach((b) => b.classList.toggle('active', b === button));
      syncSliders();
    });
  });

  panel.querySelectorAll('[data-preset]').forEach((button) => {
    button.addEventListener('click', () => {
      const preset = presets[button.dataset.preset];
      apply(left, preset.left);
      apply(right, preset.right);
      syncSliders();
    });
  });

  panel.querySelector('.reset').addEventListener('click', () => {
    for (const part of targets[selected]) apply(part, defaults);
    syncSliders();
  });

  syncSliders();
}
