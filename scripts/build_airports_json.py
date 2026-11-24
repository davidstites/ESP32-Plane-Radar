#!/usr/bin/env python3
"""Build site/setup/data/airports.json for the location setup page from OurAirports.

Each entry is [ident, iata, name, lat, lon, rank] where rank is 0 for large, 1 for medium
and 2 for small airports (the page lists bigger airports first). Large, medium and small airports are kept when
they have an IATA code or an ICAO-style (four letters) ident; local strips with codes like
"CA23" are left out to keep the file small.
"""

from __future__ import annotations

import json
from pathlib import Path

from build_large_airports import AIRPORTS_URL, fetch_csv

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "site" / "setup" / "data" / "airports.json"

TYPE_RANK = {"large_airport": 0, "medium_airport": 1, "small_airport": 2}
COORD_DECIMALS = 5


def coord(text: str | None) -> float | None:
    if not text or not text.strip():
        return None
    return round(float(text), COORD_DECIMALS)


def airport_rows(rows: list[dict[str, str]]) -> list[list]:
    kept = []
    for a in rows:
        if a.get("type") not in TYPE_RANK:
            continue
        ident = (a.get("ident") or "").strip()
        iata = (a.get("iata_code") or "").strip()
        if not iata and not (len(ident) == 4 and ident.isalpha()):
            continue
        lat = coord(a.get("latitude_deg"))
        lon = coord(a.get("longitude_deg"))
        if lat is None or lon is None:
            continue
        kept.append([ident, iata, (a.get("name") or "").strip(), lat, lon, TYPE_RANK[a["type"]]])
    kept.sort(key=lambda r: r[0])
    return kept


def main() -> None:
    rows = airport_rows(fetch_csv(AIRPORTS_URL))
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(rows, separators=(",", ":"), ensure_ascii=False) + "\n",
                   encoding="utf-8")
    print(f"Wrote {len(rows)} airports to {OUT.relative_to(ROOT)} ({OUT.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
