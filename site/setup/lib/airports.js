const IDENT = 0;
const IATA = 1;
const NAME = 2;
const LAT = 3;
const LON = 4;
const RANK = 5;

function toResult(row) {
  const label = [row[IDENT], row[IATA], row[NAME]].filter(Boolean).join(' · ');
  return { ident: row[IDENT], iata: row[IATA], name: row[NAME], lat: row[LAT], lon: row[LON], label };
}

/** True when `q` occurs in `name` at the start of a word. */
function startsAWord(name, q) {
  for (let i = name.indexOf(q); i !== -1; i = name.indexOf(q, i + 1)) {
    if (i === 0 || !/[a-z0-9]/.test(name[i - 1])) {
      return true;
    }
  }
  return false;
}

const bySize = (a, b) => a[RANK] - b[RANK];

/**
 * Looks up airports in the airports.json rows ([ident, iata, name, lat, lon, rank]).
 * Ranking: exact ident, exact IATA, ident prefix, name word-start, name substring; within a
 * group larger airports (lower rank) come first.
 */
export function searchAirports(index, query, limit = 5) {
  const q = typeof query === 'string' ? query.trim().toLowerCase() : '';
  if (q.length < 2) {
    return [];
  }
  const buckets = [[], [], [], [], []];
  for (const row of index) {
    const ident = row[IDENT].toLowerCase();
    const name = row[NAME].toLowerCase();
    if (ident === q) {
      buckets[0].push(row);
    } else if (row[IATA].toLowerCase() === q) {
      buckets[1].push(row);
    } else if (ident.startsWith(q)) {
      buckets[2].push(row);
    } else if (startsAWord(name, q)) {
      buckets[3].push(row);
    } else if (name.includes(q)) {
      buckets[4].push(row);
    }
  }
  return buckets.flatMap((bucket) => bucket.sort(bySize)).slice(0, limit).map(toResult);
}
