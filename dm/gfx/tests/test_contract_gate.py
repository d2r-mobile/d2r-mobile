#!/usr/bin/env python3
# dmsh_contract_gate.py 的自有测试：闸门策略（成对校验、缓存复用记录、发布元数据）按 dmsh_build.py 的实际调用顺序驱动，
# 产物用手工拼的 SPIR-V 字 + 手工反射（不经过翻译器、不用 glslc），所以期望与实现无关；这里不翻译任何着色器。
# Own tests for dmsh_contract_gate.py: the gate policy (pair validation, cache-reuse record, publish metadata) is driven in
# the same order dmsh_build.py uses. Artifacts are hand-assembled SPIR-V words plus hand-written reflection (no translator,
# no glslc), so expectations are independent of the implementation. Nothing here translates a shader.
import ast, json, os, sys, tempfile, unittest


def read_text(path):
    with open(path, encoding="utf-8") as f:
        return f.read()

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "tools"))
sys.path.insert(0, HERE)
import dmsh_contract_gate as gate
import dmsh_spirv_contract as contract
import test_spirv_contract as hand


def _write(path, data, binary=False):
    with open(path, "wb" if binary else "w") as f:
        f.write(data)


def write_pair(directory, key, body, res, spv_mode="ok", res_mode="ok"):
    spv = os.path.join(directory, key + ".spv")
    resp = os.path.join(directory, key + ".res.json")
    data = hand.words_of(hand.module(body))
    if spv_mode == "ok":
        _write(spv, data, binary=True)
    elif spv_mode == "empty":
        _write(spv, b"", binary=True)
    elif spv_mode == "truncated":
        _write(spv, data[:20], binary=True)
    elif spv_mode == "absent":
        pass
    if res_mode == "ok":
        _write(resp, json.dumps(dict(res, key=key)))
    elif res_mode == "absent":
        pass
    return spv, resp


class Gate(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = self.tmp.name
        self.gid = gate.contract_id()

    def tearDown(self):
        self.tmp.cleanup()

    # —— 产物成对校验 | pair validation ————————————————————————————————————————————————

    def test_valid_vertex_pair_accepted(self):
        spv, resp = write_pair(self.d, "k_vs", hand.vertex_module(), hand.VS_RES)
        self.assertEqual(gate.validate_pair("k_vs", spv, resp), (True, ""))

    def test_valid_compute_pair_accepted_with_declared_local(self):
        spv, resp = write_pair(self.d, "k_cs", hand.compute_module(x=8, y=4, z=1), hand.CS_RES)
        self.assertEqual(gate.validate_pair("k_cs", spv, resp, (8, 4, 1)), (True, ""))

    def test_binding_drift_rejected_with_key(self):
        # 反射多声明一个不存在的绑定：模块本身合法，但一对产物不符 | reflection declares a binding the module lacks
        res = dict(hand.VS_RES, cbufs=[{"binding": 0, "slot": 0}, {"binding": 7, "slot": 1}])
        spv, resp = write_pair(self.d, "k_drift", hand.vertex_module(), res)
        ok, why = gate.validate_pair("k_drift", spv, resp)
        self.assertFalse(ok)
        self.assertIn("k_drift", why)

    def test_stage_drift_rejected(self):
        # 顶点模块配计算反射：执行模型与 stage 不符 | vertex module with compute reflection
        spv, resp = write_pair(self.d, "k_stage", hand.vertex_module(), hand.CS_RES)
        ok, why = gate.validate_pair("k_stage", spv, resp)
        self.assertFalse(ok)
        self.assertIn("k_stage", why)

    def test_localsize_drift_rejected(self):
        spv, resp = write_pair(self.d, "k_local", hand.compute_module(x=8, y=4, z=1), hand.CS_RES)
        ok, why = gate.validate_pair("k_local", spv, resp, (8, 4, 2))
        self.assertFalse(ok)
        self.assertIn("LocalSize", why)

    def test_missing_reflection_rejected(self):
        spv, resp = write_pair(self.d, "k_half", hand.vertex_module(), hand.VS_RES, res_mode="absent")
        ok, why = gate.validate_pair("k_half", spv, resp)
        self.assertFalse(ok)
        self.assertIn("反射产物缺失", why)

    def test_missing_spirv_rejected(self):
        spv, resp = write_pair(self.d, "k_nospv", hand.vertex_module(), hand.VS_RES, spv_mode="absent")
        ok, why = gate.validate_pair("k_nospv", spv, resp)
        self.assertFalse(ok)
        self.assertIn("SPIR-V 产物缺失", why)

    def test_empty_and_truncated_artifacts_rejected(self):
        spv, resp = write_pair(self.d, "k_empty", hand.vertex_module(), hand.VS_RES, spv_mode="empty")
        ok, why = gate.validate_pair("k_empty", spv, resp)
        self.assertFalse(ok)
        self.assertIn("空文件", why)
        spv, resp = write_pair(self.d, "k_trunc", hand.vertex_module(), hand.VS_RES, spv_mode="truncated")
        ok, why = gate.validate_pair("k_trunc", spv, resp)
        self.assertFalse(ok)
        self.assertIn("k_trunc", why)

    # —— 缓存复用记录 | cache-reuse record ———————————————————————————————————————————————

    def test_legacy_entry_without_record_is_not_reusable(self):
        meta = os.path.join(self.d, "k.json")
        json.dump({"digest": "d", "rule_fp": "r", "code_fp": "c"}, open(meta, "w"))
        ok, why = gate.record_usable(meta, self.gid)
        self.assertFalse(ok)
        self.assertIn("验证记录", why)

    def test_entry_from_another_gate_version_is_not_reusable(self):
        meta = os.path.join(self.d, "k.json")
        json.dump({"digest": "d", "contract": "0000000000000000"}, open(meta, "w"))
        ok, _ = gate.record_usable(meta, self.gid)
        self.assertFalse(ok)

    def test_missing_meta_is_not_reusable(self):
        ok, why = gate.record_usable(os.path.join(self.d, "nope.json"), self.gid)
        self.assertFalse(ok)
        self.assertIn("无缓存元数据", why)

    def test_stamped_entry_is_reusable(self):
        meta = os.path.join(self.d, "k.json")
        base = {"digest": "d", "rule_fp": "r", "code_fp": "c"}
        json.dump(gate.stamp(base, self.gid), open(meta, "w"))
        self.assertEqual(gate.record_usable(meta, self.gid), (True, ""))

    def test_stamp_does_not_mutate_input(self):
        base = {"digest": "d"}
        stamped = gate.stamp(base, self.gid)
        self.assertNotIn("contract", base)
        self.assertEqual(stamped["contract"], self.gid)
        self.assertEqual(stamped["digest"], "d")

    def test_published_reflection_crossed_key_is_not_reusable(self):
        refl = os.path.join(self.d, "x_b.json")
        json.dump({"route": "B", "dm_ctl": "other", "chain": "c1", "stage": "vs"}, open(refl, "w"))
        ok, why = gate.published_usable(refl, "x", "c1")
        self.assertFalse(ok)
        self.assertIn("键/链/路线", why)

    def test_published_reflection_corrupt_is_not_reusable(self):
        refl = os.path.join(self.d, "x_b.json")
        open(refl, "w").write("{not json")
        ok, why = gate.published_usable(refl, "x", "c1")
        self.assertFalse(ok)
        self.assertIn("缺失或损坏", why)

    def test_published_reflection_unknown_stage_is_not_reusable(self):
        refl = os.path.join(self.d, "x_b.json")
        json.dump({"route": "B", "dm_ctl": "x", "chain": "c1", "stage": "hs"}, open(refl, "w"))
        ok, why = gate.published_usable(refl, "x", "c1")
        self.assertFalse(ok)
        self.assertIn("阶段", why)

    # —— 按 dmsh_build.py 的顺序走一遍 | the dmsh_build.py decision sequence —————————————————

    def test_build_sequence_reject_then_reuse_then_rebuild(self):
        key, chain = "k_seq", "c1"
        out = os.path.join(self.d, "b-" + chain)
        cas_meta = os.path.join(self.d, "cas", "index", key + ".json")
        os.makedirs(out)
        os.makedirs(os.path.dirname(cas_meta))

        # 1) 候选产物有漂移：闸门拒绝，已发布产物必须原样保留 | drifted candidate: rejected, published output untouched
        published = os.path.join(out, key + "_b.json")
        json.dump({"route": "B", "dm_ctl": key, "chain": chain, "stage": "vs", "key": key + "_b"}, open(published, "w"))
        before = open(published, "rb").read()
        res = dict(hand.VS_RES, cbufs=[{"binding": 0, "slot": 0}, {"binding": 9, "slot": 2}])
        spv, resp = write_pair(self.d, key, hand.vertex_module(), res)
        ok, why = gate.validate_pair(key, spv, resp)
        self.assertFalse(ok)
        self.assertIn(key, why)
        self.assertEqual(open(published, "rb").read(), before)   # 没有被覆盖 | not overwritten
        self.assertFalse(gate.record_usable(cas_meta, self.gid)[0])   # 也没写验证记录 | no record written

        # 2) 同一键的合法产物发布后，元数据带记录，命中判定通过 | a valid build publishes the record and hits pass
        spv, resp = write_pair(self.d, key, hand.vertex_module(), hand.VS_RES)
        ok, why = gate.validate_pair(key, spv, resp)
        self.assertTrue(ok, why)
        json.dump(gate.stamp({"digest": "d", "rule_fp": "r", "code_fp": "c"}, self.gid), open(cas_meta, "w"))
        self.assertTrue(gate.record_usable(cas_meta, self.gid)[0])
        self.assertTrue(gate.published_usable(published, key, chain)[0])

        # 3) 发布反射损坏：记录还在也不能复用 | corrupt published reflection: record alone is not enough
        open(published, "w").write("{}")
        self.assertTrue(gate.record_usable(cas_meta, self.gid)[0])
        self.assertFalse(gate.published_usable(published, key, chain)[0])

    def test_contract_id_is_stable_and_tied_to_abi(self):
        self.assertEqual(gate.contract_id(), self.gid)
        self.assertRegex(self.gid, r"^[0-9a-f]{16}$")
        self.assertIn("vulkan", gate.CONTRACT_ABI)
        self.assertIn("1.3", gate.CONTRACT_ABI)

    def test_build_script_wires_the_gate_where_it_matters(self):
        # 接线检查（真实路径行为见 test_build_gate_paths.py）：调用点缺一个，闸门就有旁路。 | Wiring check (real-path
        # behaviour lives in test_build_gate_paths.py): a missing call site would leave a bypass.
        src = read_text(os.path.join(HERE, "..", "tools", "dmsh_build.py"))
        tree = ast.parse(src)
        calls = {n.func.attr for n in ast.walk(tree)
                 if isinstance(n, ast.Call) and isinstance(n.func, ast.Attribute)
                 and isinstance(n.func.value, ast.Name) and n.func.value.id == "gate"}
        self.assertTrue({"validate_pair", "candidate_usable", "stamp", "pair_digests"} <= calls, calls)
        self.assertIn('DMSH_CONTRACT', src)
        self.assertIn("def final_exit", src)
        self.assertIn("_code = final_exit(res)", src)
        tail = [n for n in tree.body if isinstance(n, ast.If)][-1]
        self.assertIn("_code", ast.dump(tail.test))
        self.assertIn("exit", ast.dump(tail))


if __name__ == "__main__":
    unittest.main(verbosity=1)
