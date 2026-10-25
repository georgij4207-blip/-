import { GROUPS } from '../coyote/skeleton.js';
import { POSES } from '../coyote/poses.js';

// Панели «Позы» и «Суставы» для единого скелета.
// Ползунки строятся из описания суставов, поэтому новый сустав в
// skeleton.js сам появляется в интерфейсе.
export function setupSkeletonPanels(skeleton) {
  const poseButtons = document.getElementById('pose-buttons');
  const feetToggle = document.getElementById('feet-on-floor');
  const symToggle = document.getElementById('symmetric');
  const groupChips = document.getElementById('joint-groups');
  const sliderBox = document.getElementById('joint-sliders');

  let currentGroup = GROUPS[0][0];
  let rows = []; // { joint, channel, input, output, row }

  const format = (channel, v) => `${Math.round(v)}${channel.kind === 'rot' ? '°' : ' см'}`;

  // --- Ползунки выбранной части тела ---
  function renderGroup() {
    sliderBox.replaceChildren();
    rows = [];
    for (const joint of skeleton.joints.filter((j) => j.group === currentGroup)) {
      const box = document.createElement('div');
      box.className = 'joint';
      const title = document.createElement('h3');
      title.textContent = joint.label;
      box.append(title);

      for (const channel of joint.channels) {
        const row = document.createElement('label');
        row.className = 'slider';
        const caption = document.createElement('span');
        caption.className = 'slider-label';
        caption.textContent = channel.label;
        const output = document.createElement('output');
        const input = document.createElement('input');
        input.type = 'range';
        input.min = channel.min;
        input.max = channel.max;
        input.step = 1;
        row.append(caption, output, input);
        box.append(row);

        input.addEventListener('input', () => {
          const v = Number(input.value);
          skeleton.setValue(joint.id, channel.key, v);
          if (symToggle.checked && joint.pair) skeleton.setValue(joint.pair, channel.key, v);
          clearActivePose();
          syncSliders();
        });
        rows.push({ joint, channel, input, output, row });
      }
      sliderBox.append(box);
    }
    syncSliders();
  }

  // Показать текущие значения (после позы, IK или зеркала)
  function syncSliders() {
    for (const { joint, channel, input, output, row } of rows) {
      const v = joint.values[channel.key];
      input.value = v;
      output.textContent = format(channel, v);
      const locked = skeleton.isIkChannel(joint, channel.key);
      input.disabled = locked;
      row.classList.toggle('locked', locked);
    }
  }

  for (const [id, label] of GROUPS) {
    const chip = document.createElement('button');
    chip.type = 'button';
    chip.textContent = label;
    chip.classList.toggle('active', id === currentGroup);
    chip.addEventListener('click', () => {
      currentGroup = id;
      groupChips.querySelectorAll('button').forEach((b) => b.classList.toggle('active', b === chip));
      renderGroup();
    });
    groupChips.append(chip);
  }

  // --- Готовые позы ---
  function clearActivePose() {
    poseButtons.querySelectorAll('button').forEach((b) => b.classList.remove('active'));
  }

  for (const [id, { label, pose }] of Object.entries(POSES)) {
    const button = document.createElement('button');
    button.type = 'button';
    button.textContent = label;
    button.classList.toggle('active', id === 'stand');
    button.addEventListener('click', () => {
      skeleton.setPose(pose);
      clearActivePose();
      button.classList.add('active');
    });
    poseButtons.append(button);
  }

  feetToggle.addEventListener('change', () => {
    skeleton.feetOnFloor = feetToggle.checked;
    syncSliders();
  });

  renderGroup();

  // sync() — обновить ползунки, пока поза плавно меняется
  return { sync: syncSliders };
}
