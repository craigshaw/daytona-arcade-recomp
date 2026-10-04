"""Synthetic checks for capture alignment, pixel measurements and PNG encoding."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

spec = importlib.util.spec_from_file_location("widescreen_compare", Path(__file__).resolve().parents[1] / "scripts/widescreen_compare.py")
compare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compare)


class CaptureTests(unittest.TestCase):
    def test_centre_crop_preserves_rows_and_ignores_side_pixels(self):
        pixels = [99, 1, 2, 3, 88, 77, 4, 5, 6, 66]
        raw = struct.pack("<10I", *pixels)
        result = compare.crop(raw, 5, 3, 2)
        self.assertEqual(struct.unpack("<6I", result), (1, 2, 3, 4, 5, 6))
        self.assertEqual(compare.difference(result, struct.pack("<6I", 1, 2, 3, 4, 5, 7))["changed_pixels"], 1)

    def test_alpha_does_not_count_as_rgb_change(self):
        self.assertEqual(compare.difference(bytes([1, 2, 3, 0]), bytes([1, 2, 3, 255]))["changed_pixels"], 0)
        with self.assertRaises(ValueError):
            compare.difference(b"1234", b"12345678")

    def test_png_has_correct_channels_and_row_boundaries(self):
        encoded = compare.png(bytes([1, 2, 3, 255, 4, 5, 6, 0]), 1, 2)
        pos, pixels = 8, b""
        while pos < len(encoded):
            size = struct.unpack(">I", encoded[pos:pos + 4])[0]
            kind, body = encoded[pos + 4:pos + 8], encoded[pos + 8:pos + 8 + size]
            if kind == b"IDAT":
                pixels += body
            pos += size + 12
        self.assertEqual(zlib.decompress(pixels), bytes([0, 3, 2, 1, 0, 6, 5, 4]))

    def test_capture_window_uses_absolute_frame_numbers(self):
        self.assertEqual(compare.expected_frames(1200, 600, 0), [600, 1200])
        self.assertEqual(compare.expected_frames(1200, 600, 601), [1200])
        self.assertEqual(compare.expected_frames(605, 1, 600), list(range(600, 606)))

    def test_missing_and_wrong_sized_captures_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            with self.assertRaises(ValueError):
                compare.validate_capture(folder, 496, [600])
            (folder / "run_00600.rgb").write_bytes(b"wrong size")
            with self.assertRaises(ValueError):
                compare.validate_capture(folder, 496, [600])


if __name__ == "__main__":
    unittest.main()
