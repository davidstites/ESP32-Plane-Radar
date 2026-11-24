import test from 'node:test';
import assert from 'node:assert/strict';
import { parseLatLon } from '../setup/lib/coords.js';

test('parseLatLon accepts comma or space separated decimal degrees', () => {
  assert.deepEqual(parseLatLon('37.7749, -122.4194'), { lat: 37.7749, lon: -122.4194 });
  assert.deepEqual(parseLatLon('37.7749 -122.4194'), { lat: 37.7749, lon: -122.4194 });
  assert.deepEqual(parseLatLon('  37.7749,-122.4194  '), { lat: 37.7749, lon: -122.4194 });
  assert.deepEqual(parseLatLon('0, 0'), { lat: 0, lon: 0 });
  assert.deepEqual(parseLatLon('-90, 180'), { lat: -90, lon: 180 });
});

test('parseLatLon explains swapped values', () => {
  const r = parseLatLon('-122.4194, 37.7749');
  assert.match(r.error, /latitude/i);
  assert.match(r.error, /swap/i);
});

test('parseLatLon rejects out of range values', () => {
  assert.match(parseLatLon('91, 200').error, /latitude/i);
  assert.match(parseLatLon('0, 181').error, /longitude/i);
});

test('parseLatLon rejects anything that is not two numbers', () => {
  for (const input of ['abc', '', '   ', '1,2,3', '37.7749', '37.7abc, 10', '37.7749; -122.4194', 'nan, 0', 'Infinity, 0']) {
    const r = parseLatLon(input);
    assert.equal(typeof r.error, 'string', JSON.stringify(input));
  }
});

test('parseLatLon rejects non-strings', () => {
  assert.equal(typeof parseLatLon(undefined).error, 'string');
});
