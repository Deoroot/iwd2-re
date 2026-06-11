from __future__ import annotations

import struct
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

from reagent_asset_names import AssetResolver, TlkFile  # noqa: E402


def write_tlk(path: Path, strings: list[str]) -> None:
    encoded = [value.encode("cp1252") for value in strings]
    string_offset = 18 + len(strings) * 26
    entries = bytearray()
    offset = 0
    for value in encoded:
        entries += struct.pack("<H8sIIII", 1, b"\0" * 8, 0, 0, offset, len(value))
        offset += len(value)
    path.write_bytes(
        b"TLK V1  "
        + struct.pack("<HII", 0, len(strings), string_offset)
        + entries
        + b"".join(encoded)
    )


class AssetNamesTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.assets = self.root / "assets"
        for name in ("SPL", "ITM", "CRE", "STO", "WMP"):
            (self.assets / name).mkdir(parents=True)
        self.tlk_path = self.root / "dialog.tlk"
        write_tlk(
            self.tlk_path,
            ["", "Fireball", "Targos Docks", "Long Sword", "Sword"],
        )
        self.tlk = TlkFile(self.tlk_path)
        self.resolver = AssetResolver(self.assets, self.tlk)

    def tearDown(self) -> None:
        self.temp.cleanup()

    def test_reads_tlk_entry(self) -> None:
        entry = self.tlk.get(1)
        self.assertIsNotNone(entry)
        self.assertEqual(entry.text, "Fireball")
        self.assertIsNone(self.tlk.get(9999999))

    def test_resolves_spell_name(self) -> None:
        data = bytearray(130)
        data[:8] = b"SPL V2.0"
        struct.pack_into("<II", data, 8, 1, 9999999)
        (self.assets / "SPL" / "SPWI304.SPL").write_bytes(data)

        result = self.resolver.resolve("SPWI304")[0]

        self.assertEqual(result.compact(), "Fireball (SPWI304.SPL, strref 1)")

    def test_prefers_identified_item_name(self) -> None:
        data = bytearray(130)
        data[:8] = b"ITM V2.0"
        struct.pack_into("<II", data, 8, 4, 3)
        (self.assets / "ITM" / "SW1H01.ITM").write_bytes(data)

        result = self.resolver.resolve("SW1H01.ITM")[0]

        self.assertEqual(result.name, "Long Sword")
        self.assertEqual(result.strref, 3)

    def test_resolves_area_through_world_map(self) -> None:
        map_offset = 16
        area_offset = map_offset + 184
        data = bytearray(area_offset + 240)
        data[:8] = b"WMAPV1.0"
        struct.pack_into("<II", data, 8, 1, map_offset)
        data[map_offset : map_offset + 8] = b"WMAP1\0\0\0"
        struct.pack_into("<II", data, map_offset + 32, 1, area_offset)
        data[area_offset : area_offset + 8] = b"AR1000\0\0"
        data[area_offset + 8 : area_offset + 16] = b"AR1000\0\0"
        struct.pack_into("<II", data, area_offset + 64, 2, 2)
        (self.assets / "WMP" / "WORLDMAP.WMP").write_bytes(data)
        (self.assets / "WMP" / "WMAP1.WMP").write_bytes(data)

        results = self.resolver.resolve("AR1000", "ARE")
        result = results[0]

        self.assertEqual(len(results), 1)
        self.assertEqual(result.name, "Targos Docks")
        self.assertEqual(result.metadata["world_map"], "WMAP1")


if __name__ == "__main__":
    unittest.main()
