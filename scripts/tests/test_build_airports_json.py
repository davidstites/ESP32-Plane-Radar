"""Unit tests for build_airports_json.py's filtering and shaping (no network).

Run: python3 -m unittest discover -s scripts/tests
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import build_airports_json as bai  # noqa: E402


def row(**overrides):
    base = {
        "ident": "KSFO",
        "type": "large_airport",
        "name": "San Francisco International Airport",
        "latitude_deg": "37.618999",
        "longitude_deg": "-122.375",
        "iata_code": "SFO",
    }
    base.update(overrides)
    return base


class AirportRowsTest(unittest.TestCase):
    def test_keeps_icao_style_ident(self):
        rows = bai.airport_rows([row(ident="KHAF", type="small_airport", iata_code="")])
        self.assertEqual([r[0] for r in rows], ["KHAF"])

    def test_keeps_row_with_iata_even_if_ident_not_four_letters(self):
        rows = bai.airport_rows([row(ident="CA23", type="small_airport", iata_code="ABC")])
        self.assertEqual([r[0] for r in rows], ["CA23"])

    def test_drops_non_airport_types(self):
        for kind in ("heliport", "closed", "seaplane_base", "balloonport"):
            self.assertEqual(bai.airport_rows([row(type=kind)]), [], kind)

    def test_drops_non_icao_without_iata(self):
        self.assertEqual(
            bai.airport_rows([row(ident="CA23", type="small_airport", iata_code="")]), []
        )

    def test_drops_rows_without_coordinates(self):
        self.assertEqual(bai.airport_rows([row(latitude_deg="")]), [])
        self.assertEqual(bai.airport_rows([row(longitude_deg="  ")]), [])

    def test_rounds_to_five_decimals_and_sorts_by_ident(self):
        rows = bai.airport_rows(
            [
                row(ident="KSJC", iata_code="SJC", latitude_deg="37.3626", longitude_deg="-121.929"),
                row(),
            ]
        )
        self.assertEqual([r[0] for r in rows], ["KSFO", "KSJC"])
        self.assertEqual(rows[0][3:5], [37.619, -122.375])

    def test_shape_is_ident_iata_name_lat_lon_rank(self):
        rows = bai.airport_rows([row()])
        self.assertEqual(
            rows, [["KSFO", "SFO", "San Francisco International Airport", 37.619, -122.375, 0]]
        )

    def test_rank_orders_large_before_medium_before_small(self):
        rows = bai.airport_rows(
            [
                row(ident="KAAA", type="small_airport", iata_code=""),
                row(ident="KBBB", type="medium_airport", iata_code=""),
                row(ident="KCCC", type="large_airport", iata_code=""),
            ]
        )
        self.assertEqual({r[0]: r[5] for r in rows}, {"KAAA": 2, "KBBB": 1, "KCCC": 0})

    def test_missing_iata_becomes_empty_string(self):
        rows = bai.airport_rows([row(iata_code=None)])
        self.assertEqual(rows[0][1], "")


if __name__ == "__main__":
    unittest.main()
