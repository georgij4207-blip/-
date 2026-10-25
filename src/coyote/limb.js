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

