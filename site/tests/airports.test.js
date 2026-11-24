import test from 'node:test';
import assert from 'node:assert/strict';
import { searchAirports } from '../setup/lib/airports.js';

// [ident, iata, name, lat, lon, rank] with rank 0 large, 1 medium, 2 small.
const INDEX = [
  ['KAAA', '', 'Aksf Field', 40, -100, 2],
  ['KABC', 'ABC', '', 41, -101, 2],
  ['KSFO', 'SFO', 'San Francisco International Airport', 37.61981, -122.37482, 0],
  ['KSFZ', '', 'North Central State Airport', 41.9208, -71.4914, 2],
  ['KSJC', 'SJC', 'Norman Y. Mineta San Jose International Airport', 37.3626, -121.92902, 0],
  ['KXYZ', '', 'SFO Heliport Memorial Field', 35, -90, 2],
  ['KAPR', '', 'Allen Parish Airport', 30.75, -92.68, 2],
  ['KLDN', '', 'London Fields', 42, -83, 2],
  ['LFPO', 'ORY', 'Paris Orly Airport', 48.72333, 2.37944, 1],
  ['EGLC', 'LCY', 'London City Airport', 51.50528, 0.05528, 1],
  ['EGLL', 'LHR', 'London Heathrow Airport', 51.4706, -0.46194, 0],
  ['LFPG', 'CDG', 'Paris Charles de Gaulle Airport', 49.00969, 2.54792, 0],
];

const idents = (results) => results.map((r) => r.ident);

test('an ICAO code matches exactly, ignoring case and whitespace', () => {
  assert.equal(searchAirports(INDEX, 'ksfo')[0].ident, 'KSFO');
  assert.equal(searchAirports(INDEX, ' KSFO ')[0].ident, 'KSFO');
});

test('an IATA code finds the airport and outranks a name hit', () => {
  assert.deepEqual(idents(searchAirports(INDEX, 'SFO')), ['KSFO', 'KXYZ']);
});

test('a name matches case-insensitively', () => {
  assert.deepEqual(idents(searchAirports(INDEX, 'san francisco')), ['KSFO']);
});

test('an ident prefix lists matches and outranks a name hit', () => {
  // KAAA only matches by name ("Aksf Field"), so it comes after both prefix matches.
  assert.deepEqual(idents(searchAirports(INDEX, 'KSF')), ['KSFO', 'KSFZ', 'KAAA']);
  assert.deepEqual(idents(searchAirports(INDEX, 'ksf')), ['KSFO', 'KSFZ', 'KAAA']);
});

test('limit truncates the results', () => {
  assert.equal(searchAirports(INDEX, 'ksf', 1).length, 1);
  assert.equal(searchAirports(INDEX, 'ksf').length, 3);
});

test('short, blank and non-string queries return nothing', () => {
  assert.deepEqual(searchAirports(INDEX, ''), []);
  assert.deepEqual(searchAirports(INDEX, 'k'), []);
  assert.deepEqual(searchAirports(INDEX, '   '), []);
  assert.deepEqual(searchAirports(INDEX, undefined), []);
});

test('no match returns an empty list', () => {
  assert.deepEqual(searchAirports(INDEX, 'zzzz'), []);
});

test('results carry coordinates and a label without empty parts', () => {
  const [sfo] = searchAirports(INDEX, 'KSFO');
  assert.deepEqual(sfo, {
    ident: 'KSFO',
    iata: 'SFO',
    name: 'San Francisco International Airport',
    lat: 37.61981,
    lon: -122.37482,
    label: 'KSFO · SFO · San Francisco International Airport',
  });
  assert.equal(searchAirports(INDEX, 'KSFZ')[0].label, 'KSFZ · North Central State Airport');
  assert.equal(searchAirports(INDEX, 'KABC')[0].label, 'KABC · ABC');
});

test('names are ranked by word start first, then by airport size', () => {
  assert.deepEqual(idents(searchAirports(INDEX, 'london')), ['EGLL', 'EGLC', 'KLDN']);
  // "Allen Parish" contains "paris" mid-word, so it comes after both Paris airports.
  assert.deepEqual(idents(searchAirports(INDEX, 'paris')), ['LFPG', 'LFPO', 'KAPR']);
});

test('a name match in the middle of a word still finds the airport', () => {
  assert.deepEqual(idents(searchAirports(INDEX, 'parish')), ['KAPR']);
});

test('a word-start match works after punctuation as well as spaces', () => {
  const index = [['KDDD', '', 'Smith-Reynolds Field', 1, 2, 2]];
  assert.deepEqual(idents(searchAirports(index, 'reynolds')), ['KDDD']);
});
