import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from build import validate_map_goal_art


def png(width=1, height=1):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) + \
        chunk(b"IDAT", zlib.compress(b"\0\xff\xff\xff\xff")) + chunk(b"IEND", b"")


class MapGoalArtTest(unittest.TestCase):
    def test_complete_and_bad_art(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            names = ["cross"] + [f"sym_{i}" for i in range(7)] + [
                f"label_{i}{suffix}" for i in range(31) for suffix in ("", "_en", "_es", "_mx", "_ru")]
            for name in names:
                (root / (name + ".png")).write_bytes(png())
            self.assertEqual(len(validate_map_goal_art(root)), 163)
            sample = root / "cross.png"
            for broken in (b"", png()[:-1], png() + b"x", png(4097), png(0), png()[:-4] + b"FAIL"):
                sample.write_bytes(broken)
                with self.assertRaises(ValueError):
                    validate_map_goal_art(root)
            sample.unlink()
            with self.assertRaises(ValueError):
                validate_map_goal_art(root)
            sample.symlink_to(root / "sym_0.png")
            with self.assertRaises(ValueError):
                validate_map_goal_art(root)
            sample.unlink(); sample.write_bytes(png())
            (root / "label_0_old.png").write_bytes(png())
            self.assertEqual(len(validate_map_goal_art(root)), 164)
            (root / "unexpected.png").write_bytes(b"not packaged")
            self.assertEqual(len(validate_map_goal_art(root)), 164)


if __name__ == "__main__":
    unittest.main()
