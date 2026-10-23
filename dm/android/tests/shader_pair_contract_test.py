import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from shader_pair_contract import Violation, validate_pair, write_metadata


def instruction(opcode, *operands):
    return [(len(operands) + 1) << 16 | opcode, *operands]


def module(kind=1, image_type=3, separate=0, count=1, stage=0):
    words = [0x07230203, 0x10000, 0, 100, 0]
    words += instruction(15, {0: 0, 1: 4, 5: 5}[stage], 50, 0x6e69616d, 0)
    words += instruction(22, 1, 32)
    words += instruction(21, 5, 32, 0)
    words += instruction(43, 5, 6, count)
    if kind <= 2:
        storage = 12 if kind == 2 else 2
        words += instruction(30, 2, 1)
        words += instruction(71, 2, 2)
    else:
        storage = 0
        if image_type == 0:
            words += instruction(26, 2)
        else:
            dimension = {1: 0, 2: 5, 3: 1, 4: 2, 5: 3}[image_type & 255]
            words += instruction(25, 10, 1, dimension, int(bool(image_type & 1024)),
                                 int(bool(image_type & 256)), int(bool(image_type & 512)), 2 if kind == 4 else 1, 0)
            if kind == 3 and not separate:
                words += instruction(27, 2, 10)
            else:
                words += instruction(27, 11, 10)
    target = 2 if kind <= 2 or image_type == 0 or kind == 3 and not separate else 10
    if count != 1:
        words += instruction(28, 7, target, 6)
        target = 7
    words += instruction(32, 3, storage, target)
    words += instruction(59, 3, 4, storage)
    words += instruction(71, 4, 33, 1)
    words += instruction(71, 4, 34, 1 if stage == 1 else 0)
    metadata = struct.pack("<7I10I32I", 0x4b564d44, 1, stage, 1 if stage == 1 else 0, 1, 0, 32,
                           kind, 1, 0, 0, 0, 0, image_type, count, 0, separate, *([0] * 32))
    return words, metadata


class ShaderPairTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.spirv = Path(self.temporary.name) / "owned.spv"

    def validate(self, words, metadata):
        self.spirv.write_bytes(struct.pack(f"<{len(words)}I", *words))
        validate_pair(self.spirv, metadata)

    def test_descriptor_forms_and_arrays(self):
        for stage in (0, 1, 5):
            for kind, image_type, separate in ((0, 0, 0), (1, 0, 0), (2, 0, 0),
                    (3, 0, 1), (3, 3, 0), (3, 3, 1), (3, 2, 0), (4, 2, 0), (4, 3, 0),
                    (3, 3 | 256, 0), (3, 3 | 512, 0), (3, 3 | 1024, 0)):
                for count in (1, 4, 128):
                    with self.subTest(stage=stage, kind=kind, image_type=image_type, count=count):
                        self.validate(*module(kind, image_type, separate, count, stage))

    def test_metadata_mismatch(self):
        words, metadata = module()
        for offset, value in ((4, 2), (8, 1), (12, 1), (16, 257), (20, 1), (24, 31),
                              (28, 2), (32, 2), (56, 2)):
            changed = bytearray(metadata)
            struct.pack_into("<I", changed, offset, value)
            with self.subTest(offset=offset), self.assertRaises(Violation):
                self.validate(words, changed)
        for changed in (metadata[:-1], metadata + b"x", b"FAIL" + metadata[4:]):
            with self.assertRaises(Violation):
                self.validate(words, changed)

    def test_image_mismatch(self):
        words, metadata = module(3)
        for field, value in ((0, 4), (6, 4), (6, 3 | 256), (6, 3 | 512), (6, 3 | 1024), (9, 1)):
            changed = bytearray(metadata)
            struct.pack_into("<I", changed, 28 + field * 4, value)
            with self.subTest(field=field, value=value), self.assertRaises(Violation):
                self.validate(words, changed)

    def test_entry_and_framing(self):
        words, metadata = module()
        for offset, value in ((0, 0), (1, 0x10400), (3, 0), (4, 1), (5, 15), (6, 4), (8, 0), (9, 1)):
            changed = words.copy(); changed[offset] = value
            with self.subTest(offset=offset), self.assertRaises(Violation):
                self.validate(changed, metadata)
        for changed in (words[:5], words[:-1], words + instruction(15, 0, 51, 0x6e69616d, 0)):
            with self.assertRaises(Violation):
                self.validate(changed, metadata)

    def test_shared_support_buffer(self):
        words, metadata = module(1, stage=1)
        words += instruction(32, 13, 2, 2) + instruction(59, 13, 14, 2)
        words += instruction(71, 14, 33, 0) + instruction(71, 14, 34, 0)
        changed = bytearray(metadata[:28] + struct.pack("<10I", 0, 0, 0, 0, 0, 0, 0, 1, 0, 0) + metadata[28:])
        struct.pack_into("<I", changed, 16, 2)
        self.validate(words, changed)
        duplicate = words + instruction(59, 13, 15, 2) + instruction(71, 15, 33, 0) + instruction(71, 15, 34, 0)
        with self.assertRaises(Violation):
            self.validate(duplicate, changed)

    def test_vertex_input(self):
        words, metadata = module()
        words += instruction(23, 20, 1, 3) + instruction(32, 21, 1, 20) + instruction(59, 21, 22, 1)
        words += instruction(71, 22, 30, 7)
        with self.assertRaises(Violation):
            self.validate(words, metadata)
        changed = bytearray(metadata); struct.pack_into("<I", changed, 68 + 7 * 4, 1)
        self.validate(words, changed)
        struct.pack_into("<I", changed, 68 + 7 * 4, 3)
        with self.assertRaises(Violation):
            self.validate(words, changed)

    def test_descriptor_declaration_failures(self):
        words, metadata = module(count=4)
        for extra in (instruction(71, 4, 34, 1), instruction(71, 4, 33, 9),
                      instruction(59, 3, 8, 2) + instruction(71, 8, 34, 0) + instruction(71, 8, 33, 1)):
            with self.assertRaises(Violation):
                self.validate(words + extra, metadata)
        for length in (0, 3, 129):
            changed = words.copy()
            index = changed.index((4 << 16) | 43)
            changed[index + 3] = length
            with self.assertRaises(Violation):
                self.validate(changed, metadata)
        changed = words.copy()
        index = changed.index((4 << 16) | 28)
        changed[index + 2] = 7
        changed[changed.index((4 << 16) | 43) + 3] = 1
        with self.assertRaises(Violation):
            self.validate(changed, metadata)

    def test_unused_layout_binding(self):
        words, metadata = module()
        changed = bytearray(metadata[:68] + struct.pack("<10I", 1, 9, 3, 0, 0, 0, 0, 1, 0, 0) + metadata[68:])
        struct.pack_into("<I", changed, 16, 2)
        self.validate(words, changed)

    def test_failed_validation_preserves_previous_metadata(self):
        words, metadata = module()
        self.validate(words, metadata)
        destination = self.spirv.with_suffix(".meta")
        write_metadata(self.spirv, destination, metadata)
        with self.assertRaises(Violation):
            write_metadata(self.spirv, destination, metadata[:-1])
        self.assertEqual(destination.read_bytes(), metadata)


if __name__ == "__main__":
    unittest.main()
