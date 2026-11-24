import { parseDevice, parseToken } from './device.js';

/** The radar's /location URL for a chosen place. Throws if any part is not safe to send. */
export function buildHandoffUrl(host, lat, lon, token) {
  const device = parseDevice(host);
  if (device === null) {
    throw new Error('Device address is not a radar on your local network.');
  }
  const code = parseToken(token);
  if (code === null) {
    throw new Error('The setup code is not valid.');
  }
  const inRange = Number.isFinite(lat) && Number.isFinite(lon) && Math.abs(lat) <= 90 && Math.abs(lon) <= 180;
  if (!inRange) {
    throw new Error('Coordinates are out of range.');
  }
  return `http://${device}/location?lat=${lat.toFixed(6)}&lon=${lon.toFixed(6)}&token=${encodeURIComponent(code)}`;
}
