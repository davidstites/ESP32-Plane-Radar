import test from 'node:test';
import assert from 'node:assert/strict';
import { parseDevice, parseToken, TOKEN_ALPHABET } from '../setup/lib/device.js';

test('parseDevice rejects hosts outside the local network', () => {
  const bad = [
    'evil.com',
    '192.168.1.1@evil.com',
    'http://192.168.1.1',
    '192.168.1.1:80',
    '192.168.1.1/',
    '[::1]',
    '10.0.0.1.evil.com',
    '172.32.0.1',
    '172.15.255.255',
    '256.1.1.1',
    '1.2.3.4',
    'localhost',
    '192.168.01.1',
    '192.168.1',
    'plane-radar.local.',
    '.local',
    'a..local',
    '-x.local',
    'x-.local',
    '',
    '   ',
  ];
  for (const input of bad) {
    assert.equal(parseDevice(input), null, JSON.stringify(input));
  }
});

test('parseDevice rejects non-strings', () => {
  assert.equal(parseDevice(undefined), null);
  assert.equal(parseDevice(null), null);
  assert.equal(parseDevice(42), null);
});

test('parseDevice accepts and normalises local hosts', () => {
  assert.equal(parseDevice('PLANE-RADAR.LOCAL'), 'plane-radar.local');
  assert.equal(parseDevice(' 192.168.5.164 '), '192.168.5.164');
  assert.equal(parseDevice('10.1.2.3'), '10.1.2.3');
  assert.equal(parseDevice('172.16.0.1'), '172.16.0.1');
  assert.equal(parseDevice('172.31.255.255'), '172.31.255.255');
  assert.equal(parseDevice('169.254.1.1'), '169.254.1.1');
  assert.equal(parseDevice('my.radar.local'), 'my.radar.local');
});

test('parseToken normalises typed codes', () => {
  assert.equal(parseToken('abc 234'), 'ABC234');
  assert.equal(parseToken('abc-234'), 'ABC234');
  assert.equal(parseToken('7xsyty'), '7XSYTY');
  assert.equal(parseToken('  ABC234  '), 'ABC234');
});

test('parseToken rejects wrong lengths and ambiguous characters', () => {
  assert.equal(parseToken('ABC23'), null);
  assert.equal(parseToken('ABC2345'), null);
  assert.equal(parseToken(''), null);
  for (const ch of ['0', 'O', '1', 'I', 'L']) {
    assert.equal(parseToken(`ABC23${ch}`), null, ch);
  }
});

test('parseToken rejects non-strings', () => {
  assert.equal(parseToken(undefined), null);
  assert.equal(parseToken(null), null);
});

test('TOKEN_ALPHABET matches the firmware alphabet', () => {
  assert.equal(TOKEN_ALPHABET, '23456789ABCDEFGHJKMNPQRSTUVWXYZ');
});
