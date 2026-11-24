const DECIMAL = /^[+-]?(?:\d+\.?\d*|\.\d+)$/;
const FORMAT_HINT = 'Enter two numbers: latitude, longitude (for example 37.7749, -122.4194).';

/** Parses "lat, lon" or "lat lon" in decimal degrees. */
export function parseLatLon(input) {
  if (typeof input !== 'string') {
    return { error: FORMAT_HINT };
  }
  const parts = input.trim().split(/[\s,]+/).filter(Boolean);
  if (parts.length !== 2 || !parts.every((part) => DECIMAL.test(part))) {
    return { error: FORMAT_HINT };
  }
  const [lat, lon] = parts.map(Number);
  if (lat < -90 || lat > 90) {
    const looksSwapped = lon >= -90 && lon <= 90;
    return {
      error: looksSwapped
        ? 'Latitude must be between -90 and 90. Did you swap latitude and longitude?'
        : 'Latitude must be between -90 and 90.',
    };
  }
  if (lon < -180 || lon > 180) {
    return { error: 'Longitude must be between -180 and 180.' };
  }
  return { lat, lon };
}
