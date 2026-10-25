import * as THREE from 'three';
import { applyEarPose, DEFAULT_EAR_POSE } from '../coyote/ears.js';

const { degToRad, radToDeg } = THREE.MathUtils;

// Панель «Уши»: выбор уха (оба / левое / правое) и ползунки позы в градусах.
// «Левое» — левое ухо самого койота (справа, если смотреть анфас).
export function setupEarPanel(panel, { left, right }) {
  const targets = { both: [left, right], left: [left], right: [right] };
  let selected = 'both';

  const segButtons = panel.querySelectorAll('[data-ear]');
  const sliders = panel.querySelectorAll('input[data-param]');

  function show(slider, deg) {
    slider.value = deg;
    slider.parentElement.querySelector('output').textContent = `${Math.round(deg)}°`;
  }

  // Ползунки показывают позу первого из выбранных ушей
  function syncSliders() {
    const pose = targets[selected][0].userData.pose;
    sliders.forEach((s) => show(s, radToDeg(pose[s.dataset.param])));
  }

  sliders.forEach((slider) => {
    slider.addEventListener('input', () => {
      const deg = Number(slider.value);
      show(slider, deg);
      for (const ear of targets[selected]) applyEarPose(ear, { [slider.dataset.param]: degToRad(deg) });
    });
  });

  segButtons.forEach((button) => {
    button.addEventListener('click', () => {
      selected = button.dataset.ear;
      segButtons.forEach((b) => b.classList.toggle('active', b === button));
      syncSliders();
    });
  });

  panel.querySelector('.reset').addEventListener('click', () => {
    for (const ear of targets[selected]) applyEarPose(ear, DEFAULT_EAR_POSE);
    syncSliders();
  });

  syncSliders();
}
