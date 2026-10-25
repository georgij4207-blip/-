import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { OutlineEffect } from 'three/addons/effects/OutlineEffect.js';

// ---------- Настройки стенда ----------

const BG_COLOR = 0xeef1f5;
const OUTLINE_THICKNESS = 0.008; // толщина контура (0.003 — стандарт OutlineEffect)
const TARGET = new THREE.Vector3(0, 1, 0); // точка, вокруг которой вращается камера

// Ракурсы: азимут камеры в градусах. Персонаж смотрит в +Z,
// значит 0° — камера прямо перед ним.
const VIEWS = {
  front: 0,
  threeQuarter: 45,
  profile: 90,
  threeQuarterBack: 135,
  back: 180,
};

// ---------- Рендерер, сцена, камера ----------

const container = document.getElementById('viewport');

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2)); // планшетам с retina хватит 2x
renderer.setSize(window.innerWidth, window.innerHeight);
container.appendChild(renderer.domElement);

const effect = new OutlineEffect(renderer, {
  defaultThickness: OUTLINE_THICKNESS,
  defaultColor: [0, 0, 0],
  defaultAlpha: 1,
});

const scene = new THREE.Scene();
scene.background = new THREE.Color(BG_COLOR);
scene.fog = new THREE.Fog(BG_COLOR, 12, 30); // сетка плавно растворяется вдали

const camera = new THREE.PerspectiveCamera(40, window.innerWidth / window.innerHeight, 0.1, 100);
const FRONT_PHI = THREE.MathUtils.degToRad(75); // лёгкий взгляд сверху во всех ракурсах
camera.position.setFromSphericalCoords(6, FRONT_PHI, 0).add(TARGET);

// ---------- Свет ----------

scene.add(new THREE.HemisphereLight(0xffffff, 0xc9d2de, 1.2));

const sun = new THREE.DirectionalLight(0xffffff, 2.2);
sun.position.set(3, 6, 4);
scene.add(sun);

// ---------- Пол ----------

const noOutline = { visible: false };

// Сетка рисуется текстурой на плоскости, а не линиями GridHelper:
// часть мобильных GPU теряет линии, которые проходят за камерой.
function createGridTexture(cells = 5, size = 512) {
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = size;
  const ctx = canvas.getContext('2d');
  const step = size / cells;
  ctx.strokeStyle = '#c3cbd6';
  ctx.lineWidth = 3;
  for (let i = 1; i < cells; i++) {
    ctx.beginPath();
    ctx.moveTo(i * step, 0);
    ctx.lineTo(i * step, size);
    ctx.moveTo(0, i * step);
    ctx.lineTo(size, i * step);
    ctx.stroke();
  }
  ctx.strokeStyle = '#97a3b3'; // каждая пятая линия темнее
  ctx.lineWidth = 6;
  ctx.strokeRect(0, 0, size, size);

  const texture = new THREE.CanvasTexture(canvas);
  texture.wrapS = texture.wrapT = THREE.RepeatWrapping;
  texture.anisotropy = renderer.capabilities.getMaxAnisotropy();
  texture.colorSpace = THREE.SRGBColorSpace;
  return texture;
}

const FLOOR_SIZE = 40;
const gridTexture = createGridTexture();
gridTexture.repeat.set(FLOOR_SIZE / 5, FLOOR_SIZE / 5); // клетка = 1 единица

const grid = new THREE.Mesh(
  new THREE.PlaneGeometry(FLOOR_SIZE, FLOOR_SIZE),
  new THREE.MeshBasicMaterial({ map: gridTexture, transparent: true, depthWrite: false })
);
grid.rotation.x = -Math.PI / 2;
grid.material.userData.outlineParameters = noOutline;
scene.add(grid);

// Мягкая «пятно-тень» под персонажем: в мультяшном стиле смотрится чище
// честных теней и почти ничего не стоит планшету.
function createBlobTexture(size = 256) {
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = size;
  const ctx = canvas.getContext('2d');
  const g = ctx.createRadialGradient(size / 2, size / 2, 0, size / 2, size / 2, size / 2);
  g.addColorStop(0, 'rgba(29, 31, 36, 0.35)');
  g.addColorStop(0.6, 'rgba(29, 31, 36, 0.18)');
  g.addColorStop(1, 'rgba(29, 31, 36, 0)');
  ctx.fillStyle = g;
  ctx.fillRect(0, 0, size, size);
  return new THREE.CanvasTexture(canvas);
}

const blobShadow = new THREE.Mesh(
  new THREE.PlaneGeometry(2.6, 2.6),
  new THREE.MeshBasicMaterial({ map: createBlobTexture(), transparent: true, depthWrite: false })
);
blobShadow.rotation.x = -Math.PI / 2;
blobShadow.position.set(TARGET.x, 0.002, TARGET.z); // чуть выше сетки, чтобы не мерцало
blobShadow.renderOrder = 1;
blobShadow.material.userData.outlineParameters = noOutline;
scene.add(blobShadow);

// ---------- Мультяшный материал ----------

// Ступенчатая карта освещения: три плоских тона вместо плавного градиента.
function createToonGradient(steps = [90, 170, 255]) {
  const texture = new THREE.DataTexture(new Uint8Array(steps), steps.length, 1, THREE.RedFormat);
  texture.minFilter = THREE.NearestFilter;
  texture.magFilter = THREE.NearestFilter;
  texture.generateMipmaps = false;
  texture.needsUpdate = true;
  return texture;
}

const toonGradient = createToonGradient();

function toonMaterial(color) {
  return new THREE.MeshToonMaterial({ color, gradientMap: toonGradient });
}

// ---------- Тестовый объект ----------

const sphere = new THREE.Mesh(new THREE.SphereGeometry(1, 64, 32), toonMaterial(0xff8a3d));
sphere.position.copy(TARGET);
scene.add(sphere);

// ---------- Управление: палец — вращение, щипок — зум ----------

const controls = new OrbitControls(camera, renderer.domElement);
controls.target.copy(TARGET);
controls.enableDamping = true;
controls.dampingFactor = 0.08;
controls.enablePan = false;
controls.rotateSpeed = 0.9;
controls.minDistance = 2;
controls.maxDistance = 15;
controls.minPolarAngle = 0.15; // не даём уйти ровно в зенит
controls.maxPolarAngle = Math.PI / 2 - 0.02; // и под пол
controls.touches = { ONE: THREE.TOUCH.ROTATE, TWO: THREE.TOUCH.DOLLY_ROTATE };
controls.update();

// ---------- Кнопки ракурсов ----------

const buttons = document.querySelectorAll('.views button');
let tween = null;

function setActiveButton(view) {
  buttons.forEach((b) => b.classList.toggle('active', b.dataset.view === view));
}

function flyToView(view) {
  const offset = camera.position.clone().sub(controls.target);
  const from = new THREE.Spherical().setFromVector3(offset);
  const toTheta = THREE.MathUtils.degToRad(VIEWS[view]);

  // Поворачиваем по кратчайшей дуге
  let delta = toTheta - from.theta;
  delta = Math.atan2(Math.sin(delta), Math.cos(delta));

  tween = {
    start: performance.now(),
    duration: 600,
    from,
    deltaTheta: delta,
    toPhi: FRONT_PHI,
  };
  controls.enableDamping = false; // чтобы инерция пальца не спорила с анимацией
  setActiveButton(view);
}

function updateTween(now) {
  if (!tween) return;
  const t = Math.min((now - tween.start) / tween.duration, 1);
  const k = 1 - Math.pow(1 - t, 3); // easeOutCubic

  const s = new THREE.Spherical(
    tween.from.radius,
    THREE.MathUtils.lerp(tween.from.phi, tween.toPhi, k),
    tween.from.theta + tween.deltaTheta * k
  );
  camera.position.setFromSpherical(s).add(controls.target);

  if (t >= 1) {
    tween = null;
    controls.enableDamping = true;
  }
}

buttons.forEach((b) => b.addEventListener('click', () => flyToView(b.dataset.view)));

// Пользователь коснулся сцены — прерываем анимацию и снимаем подсветку кнопки
controls.addEventListener('start', () => {
  if (tween) {
    tween = null;
    controls.enableDamping = true;
  }
  setActiveButton(null);
});

// iOS Safari: блокируем системный зум страницы жестом
document.addEventListener('gesturestart', (e) => e.preventDefault());

// ---------- Ресайз и цикл ----------

function onResize() {
  camera.aspect = window.innerWidth / window.innerHeight;
  camera.updateProjectionMatrix();
  renderer.setSize(window.innerWidth, window.innerHeight);
}
window.addEventListener('resize', onResize);
window.addEventListener('orientationchange', onResize);

renderer.setAnimationLoop((now) => {
  updateTween(now);
  controls.update();
  effect.render(scene, camera);
});
