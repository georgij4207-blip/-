import * as THREE from 'three';

// Общие мультяшные материалы стенда.

// Объекты с этим маркером не получают чёрный контур (пол, тень, блики).
export const NO_OUTLINE = { visible: false };

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

export function toonMaterial(color) {
  return new THREE.MeshToonMaterial({ color, gradientMap: toonGradient });
}

// Плоский цвет без освещения и без контура — для бликов в глазах и на носу.
export function flatMaterial(color) {
  const material = new THREE.MeshBasicMaterial({ color });
  material.userData.outlineParameters = NO_OUTLINE;
  return material;
}

/**
 * Двухцветный toon-материал с чёткой границей, как у окраса в рисованной
 * анимации. Граница — плоскость в локальных координатах меша
 * (dot(p, normal) = offset), по желанию с волной «шерстью».
 * С отрицательной стороны плоскости — цвет `bottom`.
 */
export function twoToneMaterial({
  top,
  bottom,
  normal = new THREE.Vector3(0, 1, 0),
  offset = 0,
  waveDir = new THREE.Vector3(0, 0, 1),
  waveAmp = 0,
  waveFreq = 0,
}) {
  const material = toonMaterial(top);
  const uniforms = {
    uBottomColor: { value: new THREE.Color(bottom) },
    uSplitNormal: { value: normal.clone().normalize() },
    uSplitOffset: { value: offset },
    uWaveDir: { value: waveDir.clone().normalize() },
    uWaveAmp: { value: waveAmp },
    uWaveFreq: { value: waveFreq },
  };

  material.onBeforeCompile = (shader) => {
    Object.assign(shader.uniforms, uniforms);

    shader.vertexShader = shader.vertexShader
      .replace('#include <common>', '#include <common>\nvarying vec3 vSplitPos;')
      .replace('#include <begin_vertex>', '#include <begin_vertex>\nvSplitPos = position;');

    shader.fragmentShader = shader.fragmentShader
      .replace(
        '#include <common>',
        `#include <common>
        varying vec3 vSplitPos;
        uniform vec3 uBottomColor;
        uniform vec3 uSplitNormal;
        uniform float uSplitOffset;
        uniform vec3 uWaveDir;
        uniform float uWaveAmp;
        uniform float uWaveFreq;`
      )
      .replace(
        '#include <color_fragment>',
        `#include <color_fragment>
        float split = dot(vSplitPos, uSplitNormal) - uSplitOffset
          - uWaveAmp * sin(dot(vSplitPos, uWaveDir) * uWaveFreq);
        diffuseColor.rgb = split < 0.0 ? uBottomColor : diffuseColor.rgb;`
      );
  };
  material.customProgramCacheKey = () => 'two-tone';

  return material;
}
