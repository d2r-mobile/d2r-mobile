#!/usr/bin/env python3
# dmsh_spirv_contract.py 的自有夹具：手工拼 SPIR-V 字（不经过翻译器、不用 glslc），正例覆盖 vs/cs，
# 反例逐项破坏执行模型、绑定集合、种类、维度、描述符集、LocalSize 与模块完整性；期望全部本文件手写。
# Own fixtures for dmsh_spirv_contract.py: SPIR-V words assembled by hand (no translator, no glslc); positive cases cover
# vs/cs, negative cases break the execution model, binding set, kind, dimension, descriptor set, LocalSize and module
# integrity one at a time. All expectations are written here.
import json, os, struct, subprocess, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.join(HERE, "..", "tools", "dmsh_spirv_contract.py")

MAGIC = 0x07230203


def inst(op, *operands):
    return [((len(operands) + 1) << 16) | op, *operands]


def module(body, bound=32, version=0x00010300):
    return [MAGIC, version, 0, bound, 0] + [w for i in body for w in (i if isinstance(i, list) else [i])]


def words_of(data):
    return struct.pack("<%dI" % len(data), *data)


def header_ops(model, entry_id):
    return [inst(17, 1),                       # OpCapability Shader
            inst(14, 0, 1),                    # OpMemoryModel Logical GLSL450
            inst(15, model, entry_id, 0x6E69616D, 0)]   # OpEntryPoint <model> %entry "main"


def types_common():
    # %1 void, %2 float, %3 struct(cbuf), %10 struct(sbuf)
    return [inst(19, 1), inst(22, 2, 32),
            inst(30, 3),
            inst(30, 10)]


def vertex_module(dim=1, set_number=0, duplicate=False):
    body = header_ops(0, 20) + types_common() + [
        inst(71, 5, 33, 0), inst(71, 5, 34, set_number),          # cbuf binding 0
        inst(32, 4, 2, 3), inst(59, 4, 5, 2),
        inst(25, 6, 2, dim, 0, 0, 0, 1),                          # image <dim> sampled=1
        inst(27, 7, 6), inst(32, 8, 0, 7),
        inst(71, 9, 33, 1), inst(71, 9, 34, set_number),          # texture binding 1
        inst(59, 8, 9, 0),
        inst(71, 12, 33, 2 if not duplicate else 0), inst(71, 12, 34, set_number),   # sbuf binding 2
        inst(32, 11, 12, 10), inst(59, 11, 12, 12),
        inst(33, 13, 1), inst(54, 1, 20, 0, 13), inst(248, 21), inst(253), inst(56),
    ]
    return body


def compute_module(x=8, y=4, z=1, entry_id=20, local_target=None, dims=None, duplicate_target=None):
    target = entry_id if local_target is None else local_target
    shape = [x, y, z] if dims is None else list(dims)
    modes = [inst(16, target, 17, *shape)]
    if duplicate_target is not None:
        modes.append(inst(16, duplicate_target, 17, *shape))
    body = header_ops(5, entry_id) + modes + [inst(19, 1),
                                              inst(33, 13, 1), inst(54, 1, entry_id, 0, 13), inst(248, 21), inst(253), inst(56)]
    return body


VS_RES = {"key": "hand_vs", "stage": "vs", "notes": [], "cbufs": [{"binding": 0, "slot": 0}],
          "sbufs": [{"binding": 2, "slot": 0, "cb_slot": 0, "cb_offset": 0, "flags": 0}],
          "textures": [{"binding": 1, "type": 3, "format": "R8G8B8A8Unorm", "cbuf_slot": -1,
                        "handle": 0, "array": 0, "separate": True, "flags": 0}],
          "images": []}
CS_RES = {"key": "hand_cs", "stage": "cs", "notes": [], "cbufs": [], "sbufs": [], "textures": [], "images": []}


class Contract(unittest.TestCase):
    def run_tool(self, words, res, extra=()):
        with tempfile.TemporaryDirectory() as d:
            spv = os.path.join(d, "t.spv")
            rj = os.path.join(d, "t.res.json")
            with open(spv, "wb") as f:
                f.write(words_of(words))
            with open(rj, "w", encoding="utf-8") as f:
                json.dump(res, f)
            p = subprocess.run([sys.executable, TOOL, spv, rj, *extra], capture_output=True, text=True)
            return p.returncode, p.stdout + p.stderr

    def expect_ok(self, words, res, extra=()):
        code, out = self.run_tool(words, res, extra)
        self.assertEqual(code, 0, out)
        return out

    def expect_fail(self, words, res, needle, extra=()):
        code, out = self.run_tool(words, res, extra)
        self.assertEqual(code, 1, out)
        self.assertIn(needle, out)
        return out

    # ---- 正例 | positive ----
    def test_vertex_contract(self):
        out = self.expect_ok(module(vertex_module()), VS_RES)
        self.assertIn("stage=vs", out)
        self.assertIn("bindings=3", out)

    def test_compute_contract_with_localsize(self):
        out = self.expect_ok(module(compute_module()), CS_RES, ("--local", "8,4,1"))
        self.assertIn("local=8x4x1", out)

    # ---- 反例 | negative ----
    def test_wrong_stage_rejected(self):
        res = dict(VS_RES, stage="ps")
        self.expect_fail(module(vertex_module()), res, "执行模型")

    def test_unsupported_stage_diagnosed(self):
        res = dict(VS_RES, stage="gs")
        self.expect_fail(module(vertex_module()), res, "不支持该阶段")

    def test_missing_binding_in_module_rejected(self):
        res = json.loads(json.dumps(VS_RES))
        res["textures"][0]["binding"] = 3
        self.expect_fail(module(vertex_module()), res, "没有的绑定")

    def test_extra_module_binding_rejected(self):
        res = json.loads(json.dumps(VS_RES))
        res["textures"] = []
        self.expect_fail(module(vertex_module()), res, "没声明的绑定")

    def test_kind_mismatch_rejected(self):
        res = json.loads(json.dumps(VS_RES))
        res["cbufs"].append({"binding": 1, "slot": 0})   # 模块里 1 号是贴图 | binding 1 is a texture in the module
        res["textures"] = []
        self.expect_fail(module(vertex_module()), res, "种类不一致")

    def test_image_dimension_mismatch_rejected(self):
        body = vertex_module(dim=3)   # 模块里是 Cube | Cube in the module
        self.expect_fail(module(body), VS_RES, "维度不一致")

    def test_nonzero_descriptor_set_rejected(self):
        self.expect_fail(module(vertex_module(set_number=1)), VS_RES, "set 0")

    def test_duplicate_binding_rejected(self):
        res = json.loads(json.dumps(VS_RES))
        res["sbufs"][0]["binding"] = 0
        self.expect_fail(module(vertex_module()), res, "重复")

    def test_localsize_mismatch_rejected(self):
        self.expect_fail(module(compute_module()), CS_RES, "LocalSize", ("--local", "8,4,2"))

    def test_localsize_on_wrong_entry_rejected(self):
        # 入口函数是 20，LocalSize 却指向 29：必须按「不是入口函数」拒绝。 | entry is 20 but LocalSize targets 29.
        self.expect_fail(module(compute_module(local_target=29)), CS_RES, "不是入口函数")

    def test_localsize_zero_dimension_rejected(self):
        self.expect_fail(module(compute_module(x=0)), CS_RES, "全为正数")

    def test_localsize_truncated_rejected(self):
        self.expect_fail(module(compute_module(dims=(8, 4))), CS_RES, "契约要求 3 个")

    def test_localsize_duplicate_rejected(self):
        self.expect_fail(module(compute_module(duplicate_target=20)), CS_RES, "恰好一次")

    def test_localsize_conflicting_target_rejected(self):
        self.expect_fail(module(compute_module(duplicate_target=29)), CS_RES, "恰好一次")

    def test_local_argument_missing_value_is_usage_error(self):
        code, out = self.run_tool(module(compute_module()), CS_RES, ("--local",))
        self.assertEqual(code, 2, out)
        self.assertIn("用法错误", out)

    def test_local_argument_non_numeric_is_usage_error(self):
        code, out = self.run_tool(module(compute_module()), CS_RES, ("--local", "8,x,1"))
        self.assertEqual(code, 2, out)
        self.assertIn("正整数", out)

    def test_local_argument_zero_is_usage_error(self):
        code, out = self.run_tool(module(compute_module()), CS_RES, ("--local", "8,0,1"))
        self.assertEqual(code, 2, out)
        self.assertIn("正整数", out)

    def test_truncated_module_rejected(self):
        words = module(vertex_module())[:8]   # 切断 OpMemoryModel：字数声明 3、实际只剩 1 | cut OpMemoryModel: declares 3 words, only 1 left
        self.expect_fail(words, VS_RES, "越界")

    def test_bad_magic_rejected(self):
        words = module(vertex_module())
        words[0] = 0xDEADBEEF
        self.expect_fail(words, VS_RES, "魔数")

    def test_version_beyond_range_rejected(self):
        words = module(vertex_module(), version=0x00010400)
        self.expect_fail(words, VS_RES, "版本")


if __name__ == "__main__":
    unittest.main()
