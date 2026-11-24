import test from 'node:test';
import assert from 'node:assert/strict';
import { geocode, parseNominatim, parsePhoton } from '../setup/lib/geocode.js';

const nominatimRow = (rank, extra = {}) => ({
  lat: '37.7749',
  lon: '-122.4194',
  display_name: `Place rank ${rank}`,
  place_rank: rank,
  ...extra,
});

const photonFeature = (type, props = {}, coordinates = [-122.4194, 37.7749]) => ({
  geometry: { coordinates },
  properties: { type, name: 'Somewhere', ...props },
});

test('parseNominatim maps place_rank to precision', () => {
  const places = parseNominatim([nominatimRow(30), nominatimRow(28), nominatimRow(27), nominatimRow(26), nominatimRow(25), nominatimRow(22), nominatimRow(21), nominatimRow(16)]);
  assert.deepEqual(
    places.map((p) => p.precision),
    ['building', 'building', 'street', 'street', 'postcode', 'postcode', 'area', 'area'],
  );
  assert.deepEqual(places[0], { lat: 37.7749, lon: -122.4194, label: 'Place rank 30', precision: 'building' });
});

test('parseNominatim treats a missing place_rank as area', () => {
  const places = parseNominatim([{ lat: '1', lon: '2', display_name: 'x' }]);
  assert.equal(places[0].precision, 'area');
});

test('parseNominatim skips malformed rows and non-arrays', () => {
  const places = parseNominatim([
    null,
    nominatimRow(30, { lat: 'abc' }),
    nominatimRow(30, { lon: undefined }),
    nominatimRow(30, { display_name: undefined }),
    nominatimRow(30),
  ]);
  assert.equal(places.length, 1);
  assert.deepEqual(parseNominatim({ not: 'an array' }), []);
  assert.deepEqual(parseNominatim(null), []);
});

test('parsePhoton maps type to precision and swaps GeoJSON coordinates', () => {
  const places = parsePhoton({
    features: [photonFeature('house'), photonFeature('street'), photonFeature('postcode'), photonFeature('city')],
  });
  assert.deepEqual(
    places.map((p) => p.precision),
    ['building', 'street', 'postcode', 'area'],
  );
  assert.equal(places[0].lat, 37.7749);
  assert.equal(places[0].lon, -122.4194);
});

test('parsePhoton builds a label from the parts that exist without repeating them', () => {
  const [place] = parsePhoton({
    features: [photonFeature('house', { name: 'Main St', street: 'Main St', housenumber: '12', city: 'Springfield', country: 'USA' })],
  });
  assert.equal(place.label, 'Main St, 12 Main St, Springfield, USA');
  const [bare] = parsePhoton({ features: [photonFeature('city', { name: 'Springfield', country: 'USA' })] });
  assert.equal(bare.label, 'Springfield, USA');
  const [streetOnly] = parsePhoton({ features: [photonFeature('street', { name: undefined, street: 'Elm St' })] });
  assert.equal(streetOnly.label, 'Elm St');
});

test('parsePhoton skips malformed features and non-objects', () => {
  const places = parsePhoton({
    features: [
      null,
      { properties: { name: 'x' } },
      photonFeature('city', {}, ['a', 'b']),
      photonFeature('city', { name: undefined }),
      photonFeature('city'),
    ],
  });
  assert.equal(places.length, 1);
  assert.deepEqual(parsePhoton(null), []);
  assert.deepEqual(parsePhoton({ features: 'nope' }), []);
});

function stubFetch(handlers) {
  const urls = [];
  const fn = async (url) => {
    urls.push(String(url));
    const host = new URL(url).host;
    const handler = handlers[host];
    if (handler === 'reject') {
      throw new TypeError('network down');
    }
    return handler;
  };
  fn.urls = urls;
  return fn;
}
const ok = (body) => ({ ok: true, status: 200, json: async () => body });
const status = (code) => ({ ok: false, status: code, json: async () => ({}) });
const NOM = 'nominatim.openstreetmap.org';
const PHO = 'photon.komoot.io';

test('geocode rejects queries that are too short, too long or blank without any request', async () => {
  const fetchImpl = stubFetch({});
  for (const q of ['', '   ', 'ab', 'x'.repeat(201), undefined]) {
    const r = await geocode(q, fetchImpl);
    assert.equal(typeof r.error, 'string', JSON.stringify(q));
  }
  assert.equal(fetchImpl.urls.length, 0);
});

test('geocode uses Nominatim first and encodes the query', async () => {
  const fetchImpl = stubFetch({ [NOM]: ok([nominatimRow(30)]) });
  const r = await geocode('1 Main St & 2', fetchImpl);
  assert.equal(r.source, 'nominatim');
  assert.equal(r.places.length, 1);
  assert.equal(fetchImpl.urls.length, 1);
  assert.equal(
    fetchImpl.urls[0],
    'https://nominatim.openstreetmap.org/search?format=jsonv2&limit=5&q=1%20Main%20St%20%26%202',
  );
});

test('geocode falls back to Photon on a network error, bad status, bad JSON or no results', async () => {
  const photon = ok({ features: [photonFeature('house')] });
  const badJson = { ok: true, status: 200, json: async () => { throw new SyntaxError('bad'); } };
  for (const nominatim of ['reject', status(429), badJson, ok([])]) {
    const fetchImpl = stubFetch({ [NOM]: nominatim, [PHO]: photon });
    const r = await geocode('Springfield', fetchImpl);
    assert.equal(r.source, 'photon');
    assert.equal(fetchImpl.urls.length, 2);
    assert.equal(fetchImpl.urls[1], 'https://photon.komoot.io/api/?limit=5&q=Springfield');
  }
});

test('geocode reports an unreachable service when both fail', async () => {
  const r = await geocode('Springfield', stubFetch({ [NOM]: 'reject', [PHO]: status(500) }));
  assert.match(r.error, /unavailable/i);
});

test('geocode reports no match when both answer with nothing', async () => {
  const r = await geocode('Qwertyuiop', stubFetch({ [NOM]: ok([]), [PHO]: ok({ features: [] }) }));
  assert.match(r.error, /no place/i);
});
