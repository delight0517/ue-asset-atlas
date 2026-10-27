export function frameRateFromSamples(frameCount, elapsedMs) {
  if (!Number.isFinite(frameCount) || !Number.isFinite(elapsedMs) || frameCount < 2 || elapsedMs <= 0) return null;
  return frameCount * 1000 / elapsedMs;
}

export function nextAdaptiveDpr(currentDpr, fps, deviceDpr) {
  if (!Number.isFinite(currentDpr) || !Number.isFinite(fps) || !Number.isFinite(deviceDpr) || deviceDpr <= 0) return null;
  const ceiling = Math.min(deviceDpr, 2);
  const floor = Math.min(.6, ceiling);
  const current = Math.max(floor, Math.min(ceiling, currentDpr));
  if (fps < 24) return Math.max(floor, current * .82);
  if (fps >= 28) return Math.min(ceiling, current + .1);
  return current;
}
