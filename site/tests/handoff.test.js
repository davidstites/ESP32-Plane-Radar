import test from 'node:test';
import assert from 'node:assert/strict';
import { buildHandoffUrl } from '../setup/lib/handoff.js';

test('buildHandoffUrl builds the device /location URL', () => {
  assert.equal(
    buildHandoffUrl('192.168.5.164', 37.1476, -121.6729, '7xsyty'),
    'http://192.168.5.164/location?lat=37.147600&lon=-121.672900&token=7XSYTY',
  );
});

test('buildHandoffUrl normalises the host', () => {
  assert.equal(
    buildHandoffUrl('PLANE-RADAR.LOCAL', 0, 0, 'ABC234'),
    'http://plane-radar.local/location?lat=0.000000&lon=0.000000&token=ABC234',
  );
});

test('buildHandoffUrl refuses unsafe or invalid input', () => {
  assert.throws(() => buildHandoffUrl('evil.com', 1, 1, 'ABC234'), /device/i);
  assert.throws(() => buildHandoffUrl('192.168.5.164', 1, 1, 'nope'), /code/i);
  assert.throws(() => buildHandoffUrl('192.168.5.164', 91, 0, 'ABC234'), /coordinates/i);
  assert.throws(() => buildHandoffUrl('192.168.5.164', 0, -181, 'ABC234'), /coordinates/i);
  assert.throws(() => buildHandoffUrl('192.168.5.164', Number.NaN, 0, 'ABC234'), /coordinates/i);
});
