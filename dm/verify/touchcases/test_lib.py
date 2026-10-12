import json
import contextlib
import io
import pathlib
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).parent))

from lib import AssertFail, check, load, main


def write_jsonl(directory, name, rows):
    (directory / name).write_text(
        "".join(json.dumps(row, ensure_ascii=False) + "\n" for row in rows),
        encoding="utf-8",
    )


FIXTURE_DIR = pathlib.Path(__file__).parent / "tests" / "fixture"


class TestTouchcaseLib(unittest.TestCase):
    def test_all_interfaces(self):
        with tempfile.TemporaryDirectory() as path:
            directory = pathlib.Path(path)
            shutil.copytree(FIXTURE_DIR, directory, dirs_exist_ok=True)
            result = load(directory)
            self.assertEqual(result.items_at(2), {8: {"id": 8, "cls": 2}})
            self.assertIsNone(result.cursor_at(3))
            self.assertEqual(result.players_at(3), 2)
            self.assertEqual(result.panels_at(3), {"Inventory"})
            self.assertEqual(result.panels_at(4), set())
            self.assertEqual(result.panels_at(5), {"Inventory"})
            self.assertEqual(result.c2s_ops(), [(2, 0x19, "1900"), (5, 0x18, "18ff")])
            self.assertEqual(result.c2s_ops(3, 5), [(5, 0x18, "18ff")])

    def test_missing_state_is_red(self):
        with tempfile.TemporaryDirectory() as path:
            directory = pathlib.Path(path)
            shutil.copytree(FIXTURE_DIR, directory, dirs_exist_ok=True)
            (directory / "state.jsonl").unlink()
            with self.assertRaises(AssertFail):
                load(directory)

    def test_check_records_failures_and_main_exits_red(self):
        check(False, "反例失败")
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            result = main([])
        self.assertEqual(result, 1)
        self.assertIn("红", output.getvalue())


if __name__ == "__main__":
    unittest.main()
