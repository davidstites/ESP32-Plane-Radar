// Address search in the browser. One request per call (the public Nominatim server forbids
// autocomplete), Photon as a fallback. Both serve the same OpenStreetMap data.

const MIN_QUERY = 3;
const MAX_QUERY = 200;

function precisionFromRank(rank) {
  if (typeof rank !== 'number') {
    return 'area';
  }
  if (rank >= 28) {
    return 'building';
  }
  if (rank >= 26) {
    return 'street';
  }
  return rank >= 22 ? 'postcode' : 'area';
}

const PHOTON_PRECISION = { house: 'building', street: 'street', postcode: 'postcode' };

export function parseNominatim(json) {
  if (!Array.isArray(json)) {
    return [];
  }
  const places = [];
  for (const item of json) {
    const lat = Number(item?.lat);
    const lon = Number(item?.lon);
    if (!Number.isFinite(lat) || !Number.isFinite(lon) || typeof item.display_name !== 'string') {
      continue;
    }
    places.push({ lat, lon, label: item.display_name, precision: precisionFromRank(item.place_rank) });
  }
  return places;
}

export function parsePhoton(json) {
  const features = json?.features;
  if (!Array.isArray(features)) {
    return [];
  }
  const places = [];
  for (const feature of features) {
    const [lon, lat] = feature?.geometry?.coordinates ?? [];
    const props = feature?.properties ?? {};
    if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
      continue;
    }
    const street = props.housenumber && props.street ? `${props.housenumber} ${props.street}` : props.street;
    const label = [...new Set([props.name, street, props.city, props.country].filter(Boolean))].join(', ');
    if (label === '') {
      continue;
    }
    places.push({ lat, lon, label, precision: PHOTON_PRECISION[props.type] ?? 'area' });
  }
  return places;
}

async function lookup(fetchImpl, url, parse) {
  try {
    const response = await fetchImpl(url);
    if (!response.ok) {
      return { places: [], failed: true };
    }
    return { places: parse(await response.json()), failed: false };
  } catch {
    return { places: [], failed: true };
  }
}

/** Never throws: resolves to { places, source } or { error }. */
export async function geocode(query, fetchImpl = globalThis.fetch) {
  const q = typeof query === 'string' ? query.trim() : '';
  if (q.length < MIN_QUERY || q.length > MAX_QUERY) {
    return { error: `Enter between ${MIN_QUERY} and ${MAX_QUERY} characters to search.` };
  }
  const encoded = encodeURIComponent(q);
  const nominatim = await lookup(
    fetchImpl,
    `https://nominatim.openstreetmap.org/search?format=jsonv2&limit=5&q=${encoded}`,
    parseNominatim,
  );
  if (nominatim.places.length > 0) {
    return { places: nominatim.places, source: 'nominatim' };
  }
  const photon = await lookup(fetchImpl, `https://photon.komoot.io/api/?limit=5&q=${encoded}`, parsePhoton);
  if (photon.places.length > 0) {
    return { places: photon.places, source: 'photon' };
  }
  if (nominatim.failed || photon.failed) {
    return { error: 'Address search is unavailable right now. Try an airport code or enter coordinates.' };
  }
  return { error: 'No place found. Try a fuller address, an airport code, or enter coordinates.' };
}
