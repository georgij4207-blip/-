import * as THREE from 'three';

// Общие детали конечностей: сегменты и суставы.

// Сегмент конечности: вытянутая «капсула» от 0 до −length с разными
// радиусами концов. Скруглённые концы прячут стыки в суставах.
export function createSegmentGeometry({ length, rTop, rBottom }) {
  const points = [];
  const cap = 8;
  for (let i = 0; i <= cap; i++) {
    const a = -Math.PI / 2 + (i / cap) * (Math.PI / 2); // низ: −90° → 0°
    points.push(new THREE.Vector2(Math.cos(a) * rBottom, -length + Math.sin(a) * rBottom));
  }
  for (let i = 0; i <= cap; i++) {
    const a = (i / cap) * (Math.PI / 2); // верх: 0° → 90°
    points.push(new THREE.Vector2(Math.cos(a) * rTop, Math.sin(a) * rTop));
  }
  return new THREE.LatheGeometry(points, 20);
}

export function joint(name, y = 0) {
  const group = new THREE.Group();
  group.name = name;
  group.position.y = y;
  return group;
}


// Веса скиннинга для цепочки костей вдоль +Y (высоты костей — boneHeights):
// каждая вершина линейно делится между двумя соседними костями,
// выше последней — целиком на ней.
export function skinAlongY(geometry, boneHeights) {
  const pos = geometry.attributes.position;
  const indices = [];
  const weights = [];
  for (let i = 0; i < pos.count; i++) {
    const y = pos.getY(i);
    let bone = boneHeights.length - 1;
    let w = 1;
    for (let b = 0; b < boneHeights.length - 1; b++) {
      if (y < boneHeights[b + 1]) {
        bone = b;
        w = 1 - THREE.MathUtils.clamp((y - boneHeights[b]) / (boneHeights[b + 1] - boneHeights[b]), 0, 1);
        break;
      }
    }
    if (bone === boneHeights.length - 1) {
      indices.push(bone, 0, 0, 0);
      weights.push(1, 0, 0, 0);
    } else {
      indices.push(bone, bone + 1, 0, 0);
      weights.push(w, 1 - w, 0, 0);
    }
  }
  geometry.setAttribute('skinIndex', new THREE.Uint16BufferAttribute(indices, 4));
  geometry.setAttribute('skinWeight', new THREE.Float32BufferAttribute(weights, 4));
  return geometry;
}
