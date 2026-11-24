const IDENT = 0;
const IATA = 1;
const NAME = 2;
const LAT = 3;
const LON = 4;

function toResult(row) {
  const label = [row[IDENT], row[IATA], row[NAME]].filter(Boolean).join(' · ');
  return { ident: row[IDENT], iata: row[IATA], name: row[NAME], lat: row[LAT], lon: row[LON], label };
}

/**
 * Looks up airports in the airports.json rows ([ident, iata, name, lat, lon]).
 * Ranking: exact ident, exact IATA, ident prefix, then name substring.
 */
export function searchAirports(index, query, limit = 5) {
  const q = typeof query === 'string' ? query.trim().toLowerCase() : '';
  if (q.length < 2) {
    return [];
  }
  const buckets = [[], [], [], []];
  for (const row of index) {
    const ident = row[IDENT].toLowerCase();
    if (ident === q) {
      buckets[0].push(row);
    } else if (row[IATA].toLowerCase() === q) {
      buckets[1].push(row);
    } else if (ident.startsWith(q)) {
      buckets[2].push(row);
    } else if (row[NAME].toLowerCase().includes(q)) {
      buckets[3].push(row);
    }
  }
  return buckets.flat().slice(0, limit).map(toResult);
}
