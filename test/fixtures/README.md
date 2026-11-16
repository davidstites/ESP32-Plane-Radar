# Test fixtures

Recorded adsb.fi responses used by the `native` unit tests.

- `amsterdam_30nm.chunked` — raw HTTP/1.1 chunked body (chunk-size lines included):

  ```bash
  curl -s --http1.1 --raw https://opendata.adsb.fi/api/v3/lat/52.3676/lon/4.9041/dist/30 \
    -o test/fixtures/amsterdam_30nm.chunked
  ```

- `amsterdam_30nm.json` — the same response with the chunk framing removed, so both
  fixtures describe the same snapshot:

  ```bash
  python3 -c "
  raw = open('test/fixtures/amsterdam_30nm.chunked', 'rb').read(); out = b''; i = 0
  while True:
      j = raw.index(b'\r\n', i); n = int(raw[i:j].split(b';')[0], 16); i = j + 2
      if n == 0: break
      out += raw[i:i + n]; i += n + 2
  open('test/fixtures/amsterdam_30nm.json', 'wb').write(out)"
  ```

- `many_aircraft.json` — Heathrow 40 nm, with `ac` padded to 80 positioned entries by
  repeating real ones so it exceeds `kMaxAircraft` (64):

  ```bash
  curl -s https://opendata.adsb.fi/api/v3/lat/51.4700/lon/-0.4543/dist/40 \
    -o test/fixtures/many_aircraft.json
  python3 -c "
  import json; p = 'test/fixtures/many_aircraft.json'; m = json.load(open(p))
  pos = [a for a in m['ac'] if 'lat' in a and 'lon' in a]; i = 0
  while sum('lat' in a and 'lon' in a for a in m['ac']) < 80:
      m['ac'].append(pos[i % len(pos)]); i += 1
  json.dump(m, open(p, 'w'), separators=(',', ':'))"
  ```
