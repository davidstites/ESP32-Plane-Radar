import { parseDevice, parseToken } from './lib/device.js';
import { parseLatLon } from './lib/coords.js';
import { buildHandoffUrl } from './lib/handoff.js';
import { searchAirports } from './lib/airports.js';
import { geocode } from './lib/geocode.js';

const APPROXIMATE_M = 5000; // geolocation error (metres) beyond which a fix counts as approximate
const PRECISION_TAG = {
  building: 'Exact address',
  street: 'Street',
  postcode: 'Postcode',
  area: 'Approximate area',
};

const $ = (id) => document.getElementById(id);
const el = {
  device: $('device'),
  deviceHint: $('device-hint'),
  code: $('code'),
  codeHint: $('code-hint'),
  form: $('search-form'),
  query: $('query'),
  locate: $('locate'),
  coords: $('coords'),
  useCoords: $('use-coords'),
  searchMessage: $('search-message'),
  results: $('results'),
  scope: $('scope'),
  chosenLabel: $('chosen-label'),
  chosenCoords: $('chosen-coords'),
  chosenCaution: $('chosen-caution'),
  send: $('send'),
  sendMessage: $('send-message'),
};

const DEVICE_HINT = el.deviceHint.textContent;
const CODE_HINT = el.codeHint.textContent;
const state = { place: null };

let airportsPromise = null;
function loadAirports() {
  airportsPromise ??= fetch('data/airports.json')
    .then((response) => (response.ok ? response.json() : []))
    .catch(() => []);
  return airportsPromise;
}

function say(node, text, isError = false) {
  node.textContent = text;
  node.classList.toggle('is-error', isError);
}

function setHint(input, hint, ok, okText, badText) {
  const invalid = !ok && input.value.trim() !== '';
  input.setAttribute('aria-invalid', String(invalid));
  say(hint, invalid ? badText : okText, invalid);
}

function render() {
  const deviceOk = parseDevice(el.device.value) !== null;
  const codeOk = parseToken(el.code.value) !== null;
  setHint(el.device, el.deviceHint, deviceOk, DEVICE_HINT,
    'Use a name ending in .local, or a private address like 192.168.1.20.');
  setHint(el.code, el.codeHint, codeOk, CODE_HINT,
    'The code is six letters and numbers, without 0, O, 1, I or L.');

  const place = state.place;
  el.scope.classList.toggle('is-set', place !== null);
  el.chosenLabel.textContent = place ? place.label : 'Nothing chosen yet.';
  el.chosenCoords.textContent = place ? `${place.lat.toFixed(4)}, ${place.lon.toFixed(4)}` : '';
  el.chosenCaution.hidden = !(place && place.approximate);
  el.send.disabled = !(deviceOk && codeOk && place);
}

function choose(place) {
  // Restart the scope sweep for the new choice.
  el.scope.classList.remove('is-set');
  void el.scope.getBoundingClientRect();
  state.place = place;
  say(el.sendMessage, '');
  render();
}

function showResults(places) {
  el.results.replaceChildren();
  for (const place of places) {
    const item = document.createElement('li');
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'result';
    button.setAttribute('aria-pressed', 'false');
    const label = document.createElement('span');
    label.className = 'result-label';
    label.textContent = place.label;
    const tag = document.createElement('span');
    tag.className = 'result-tag';
    tag.textContent = place.tag;
    button.append(label, tag);
    button.addEventListener('click', () => {
      for (const other of el.results.querySelectorAll('.result')) {
        other.setAttribute('aria-pressed', String(other === button));
      }
      choose(place);
    });
    item.append(button);
    el.results.append(item);
  }
}

function isExactCode(hit, query) {
  const q = query.trim().toLowerCase();
  return hit !== undefined && (hit.ident.toLowerCase() === q || hit.iata.toLowerCase() === q);
}

async function onSearch(event) {
  event.preventDefault();
  const query = el.query.value.trim();
  showResults([]);
  say(el.searchMessage, 'Searching…');

  const hits = searchAirports(await loadAirports(), query);
  const places = hits.map((a) => ({ lat: a.lat, lon: a.lon, label: a.label, tag: 'Airport', approximate: false }));

  let error = null;
  if (!isExactCode(hits[0], query)) {
    const found = await geocode(query);
    if (found.places) {
      for (const p of found.places) {
        places.push({
          lat: p.lat,
          lon: p.lon,
          label: p.label,
          tag: PRECISION_TAG[p.precision],
          approximate: p.precision === 'postcode' || p.precision === 'area',
        });
      }
    } else {
      error = found.error;
    }
  }

  showResults(places);
  if (places.length > 0) {
    say(el.searchMessage, 'Choose the best match.');
  } else {
    say(el.searchMessage, error ?? 'No place found.', true);
  }
}

const GEO_ERRORS = {
  1: 'Location access was blocked. Allow it in your browser settings, or search for an address.',
  2: 'Your device could not work out where it is. Search for an address instead.',
  3: 'Finding your location took too long. Try again, or search for an address.',
};

function onLocate() {
  if (!navigator.geolocation) {
    say(el.searchMessage, 'This browser cannot share its location. Search for an address instead.', true);
    return;
  }
  say(el.searchMessage, 'Finding your location…');
  navigator.geolocation.getCurrentPosition(
    ({ coords }) => {
      const metres = Math.round(coords.accuracy);
      showResults([]);
      say(el.searchMessage, '');
      choose({
        lat: coords.latitude,
        lon: coords.longitude,
        label: `Your current location (within ${metres} m)`,
        tag: 'Your device',
        approximate: metres > APPROXIMATE_M,
      });
    },
    (error) => say(el.searchMessage, GEO_ERRORS[error.code] ?? GEO_ERRORS[2], true),
    { enableHighAccuracy: true, timeout: 15000 },
  );
}

function onUseCoords() {
  const parsed = parseLatLon(el.coords.value);
  if (parsed.error) {
    say(el.searchMessage, parsed.error, true);
    return;
  }
  showResults([]);
  say(el.searchMessage, '');
  choose({ lat: parsed.lat, lon: parsed.lon, label: 'Coordinates you entered', tag: 'Coordinates', approximate: false });
}

function onSend() {
  try {
    location.assign(buildHandoffUrl(el.device.value, state.place.lat, state.place.lon, el.code.value));
  } catch (error) {
    say(el.sendMessage, error.message, true);
  }
}

// The radar's link carries its address and one-time code. Read them, then take the code out
// of the address bar so it does not linger in history.
const params = new URLSearchParams(location.search);
el.device.value = params.get('device') ?? '';
el.code.value = params.get('token') ?? '';
if (params.has('token')) {
  params.delete('token');
  const query = params.toString();
  history.replaceState(null, '', `${location.pathname}${query ? `?${query}` : ''}`);
}

el.device.addEventListener('input', render);
el.code.addEventListener('input', render);
el.form.addEventListener('submit', onSearch);
el.locate.addEventListener('click', onLocate);
el.useCoords.addEventListener('click', onUseCoords);
el.send.addEventListener('click', onSend);
render();
