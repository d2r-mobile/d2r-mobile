#!/usr/bin/env python3
# dmsh_build.py 真实出包路径的自有测试：在沙箱里导入实际的 dmsh_build.py（键集为空，因此导入时不翻译任何东西），
# 然后直接调用它的 fin / cache_lookup / restore_cache / publish_cache / final_exit，外部 I/O 用受控桩注入
# （finish 桩代替 spirv-cross+metal，产物对用手工拼的 SPIR-V + 手工反射）。不跑翻译器、不跑 Metal、不用游戏数据。
# Own tests for the real dmsh_build.py paths: the actual script is imported in a sandbox (with an empty key set, so importing
# translates nothing), then its fin / cache_lookup / restore_cache / publish_cache / final_exit are called directly with
# controlled I/O stubs (a finish stub replaces spirv-cross+metal; artifact pairs are hand-assembled SPIR-V + hand reflection).
# No translator, no Metal, no game data.
import json, os, shutil, subprocess, sys, tempfile, types, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_TOOLS = os.path.abspath(os.path.join(HERE, "..", "tools"))
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, REPO_TOOLS)
sys.path.insert(0, HERE)
import dmsh_contract_gate as gate
import test_spirv_contract as hand

FAKE_CHAIN = '''HEADER = %(header)r
LOCK = %(lock)r
def check_tools():
    return []
def version():
    return "tchain"
def options():
    return ""
def xlate_source_sha():
    return "x"
'''
FAKE_FACTS = '''def load():
    return {"k": {}}
'''
FAKE_FETCH = '''def variant(*a, **k):
    return None, None
'''


def write(path, data, binary=False):
    with open(path, "wb" if binary or isinstance(data, (bytes, bytearray)) else "w") as f:
        f.write(data)


def read(path):
    with open(path, "rb") as f:
        return f.read()


class BuildPaths(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp(prefix="dmshgate_")
        cls.root = cls.tmp
        tools = os.path.join(cls.root, "tools")
        nvn = os.path.join(cls.root, "nvn")
        os.makedirs(tools)
        os.makedirs(nvn)
        for name in ("dmsh_build.py", "dmsh_contract_gate.py", "dmsh_spirv_contract.py"):
            shutil.copy(os.path.join(REPO_TOOLS, name), os.path.join(tools, name))
        for name in ("shader_map.tsv", "shader_map.tsv.conflicts.tsv", "bf_want.tsv"):
            write(os.path.join(nvn, name), "")
        header = os.path.join(cls.root, "dmsh_chain.h")
        write(header, '"tchain"\n')
        lock = os.path.join(cls.root, "lock.txt")
        write(lock, "fake lock\n")
        write(os.path.join(tools, "dmsh_chain.py"), FAKE_CHAIN % {"header": header, "lock": lock})
        write(os.path.join(tools, "dmsh_facts.py"), FAKE_FACTS)
        write(os.path.join(tools, "dmsh_fetch.py"), FAKE_FETCH)
        cls.out = os.path.join(cls.root, "outpublic")
        os.makedirs(cls.out)
        cls.out = os.path.join(cls.out, "b-tchain")   # dmsh_build.py 在输出目录后追加 b-<链版本> | the script appends b-<chain>
        os.makedirs(cls.out)
        cls.cas = os.path.join(cls.root, "cas")
        cls.w = os.path.join(cls.root, "work")
        os.makedirs(cls.cas)
        os.makedirs(cls.w)
        dm_dir = os.path.join(cls.root, "dm")
        os.makedirs(dm_dir, exist_ok=True)

        # 导入期只允许一次工具指纹探测（spirv-cross --version），其余外调一律拒绝 | only the tool fingerprint probe may run at import
        cls._real_run = subprocess.run

        def fake_run(cmd, *a, **k):
            if list(cmd[:2]) == ["spirv-cross", "--version"]:
                return types.SimpleNamespace(stdout="spirv-cross fake", stderr="", returncode=0)
            raise AssertionError("unexpected subprocess at import: %r" % (cmd,))

        subprocess.run = fake_run
        old_argv, old_env = sys.argv[:], dict(os.environ)
        sys.argv = ["dmsh_build.py", dm_dir, os.path.dirname(cls.out)]
        os.environ["DMSH_CONTRACT"] = "0"
        os.environ["DM_ROOT"] = cls.root
        os.environ["J"] = "1"
        os.environ.pop("IOSOUT", None)
        os.environ.pop("DMSH_CAS", None)
        try:
            import importlib.util
            spec = importlib.util.spec_from_file_location("dmsh_build_sandbox", os.path.join(tools, "dmsh_build.py"))
            cls.m = importlib.util.module_from_spec(spec)
            sys.path.insert(0, tools)
            try:
                spec.loader.exec_module(cls.m)
            finally:
                sys.path.remove(tools)
        finally:
            sys.argv = old_argv
            subprocess.run = cls._real_run
            os.environ.clear()
            os.environ.update(old_env)
        # 沙箱导入只应产生空构建 | the sandbox import must produce an empty build
        assert json.load(open(os.path.join(cls.out, "_b_build.json"))) == []

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def setUp(self):
        shutil.rmtree(self.out, ignore_errors=True)
        shutil.rmtree(self.cas, ignore_errors=True)
        shutil.rmtree(self.w, ignore_errors=True)
        for d in (self.out, self.cas, self.w):
            os.makedirs(d)
        m = self.m
        m.out = self.out
        m.CAS = self.cas
        m.w = self.w
        m.CONTRACT_ON = True
        m.GATE = gate.contract_id()
        m.PRE_STATS.clear()
        m.BF_STATS.clear()
        m.cache_hits = set()
        m.xerr = {}
        m.jobs = {"k": {"stage": "vs"}}
        m.c1s = {"k": b""}
        m.digests = {"k": "d1"}
        m.rule_fps = {"k": "r1"}
        m.fetch_finish = lambda key, src, refl: refl
        self.finish_calls = []

        def stub_finish(key, w, job, c1):
            self.finish_calls.append(key)
            if getattr(self, "compile_fails", False):
                return "MSL 失败", "stub compiler error", None
            write(os.path.join(m.out, key + "_b.metallib"), b"LIB-" + key.encode())
            write(os.path.join(m.out, key + "_b.metal"), "// stub msl\n")
            refl = {"key": key + "_b", "route": "B", "chain": m.CHAIN, "dm_ctl": key, "stage": "vs"}
            write(os.path.join(m.out, key + "_b.json"), json.dumps(refl))
            return "ok", "", refl

        m.finish = stub_finish
        self.compile_fails = False

    # —— 工具 | helpers ——————————————————————————————————————————————————————

    def pair(self, key="k", res=None):
        spv = os.path.join(self.w, key + ".spv")
        resp = os.path.join(self.w, key + ".res.json")
        write(spv, hand.words_of(hand.module(hand.vertex_module())), binary=True)
        write(resp, json.dumps(dict(res or hand.VS_RES, key=key)))
        return spv, resp

    def seed_good_outputs(self):
        good = {}
        for name in ("k_b.metallib", "k_b.metal", "k_b.json"):
            path = os.path.join(self.out, name)
            write(path, b"GOOD-" + name.encode())
            good[name] = read(path)
        return good

    def assert_outputs(self, good):
        for name, data in good.items():
            self.assertEqual(read(os.path.join(self.out, name)), data, name)

    def publish_valid(self):
        self.pair()
        return self.m.fin("k")

    # —— 1. 翻译/产物失败在校验模式必须非 0 | producer failures must be non-zero in validated mode ————

    def test_producer_failure_exits_nonzero(self):
        self.m.xerr = {"k": "producer failed"}
        r = self.m.fin("k")
        self.assertEqual(r[1], "翻译失败")
        self.assertEqual(self.m.final_exit([r]), 1)
        self.assertFalse(os.path.exists(os.path.join(self.out, "k_b.json")))

    def test_missing_spirv_exits_nonzero(self):
        write(os.path.join(self.w, "k.res.json"), json.dumps(hand.VS_RES))
        r = self.m.fin("k")
        self.assertEqual(r[1], "翻译失败")
        self.assertEqual(self.m.final_exit([r]), 1)

    def test_missing_reflection_is_contract_failure(self):
        write(os.path.join(self.w, "k.spv"), hand.words_of(hand.module(hand.vertex_module())), binary=True)
        r = self.m.fin("k")
        self.assertEqual(r[1], "契约不符")
        self.assertIn("反射产物缺失", r[2])
        self.assertEqual(self.m.final_exit([r]), 1)

    def test_compile_failure_exits_nonzero_and_keeps_good_outputs(self):
        self.compile_fails = True
        good = self.seed_good_outputs()
        self.pair()
        r = self.m.fin("k")
        self.assertEqual(r[1], "MSL 失败")
        self.assert_outputs(good)
        self.assertEqual(self.m.final_exit([r]), 1)
        self.assertFalse(os.path.exists(gate.cache_path("d1")) if hasattr(gate, "cache_path") else False)

    def test_contract_drift_does_not_publish(self):
        bad = dict(hand.VS_RES, cbufs=[{"binding": 0, "slot": 0}, {"binding": 5, "slot": 1}])
        self.pair(res=bad)
        r = self.m.fin("k")
        self.assertEqual(r[1], "契约不符")
        self.assertIn("k", r[2])
        self.assertFalse(os.path.exists(os.path.join(self.out, "k_b.json")))
        self.assertFalse(os.path.exists(os.path.join(self.cas, "index", "k.json")))

    # —— 2. 缓存候选先验后恢复，失败不动公共产物 | validate candidate before restore ————————————

    def test_valid_build_publishes_pair_and_cache_hit_restores(self):
        r = self.publish_valid()
        self.assertEqual(r[1], "ok")
        meta = json.load(open(os.path.join(self.cas, "index", "k.json")))
        self.assertEqual(meta["contract"], gate.contract_id())
        self.assertIn("pair", meta)
        self.assertIn("published", meta)
        served = os.path.join(self.cas, "shader", "d1", "src", "k.res.json")
        self.assertTrue(os.path.exists(served))
        for name in os.listdir(self.out):
            os.unlink(os.path.join(self.out, name))
        hit, why = self.m.cache_lookup("k", "d1", "r1", "code")
        self.assertTrue(hit, why)
        self.assertTrue(os.path.exists(os.path.join(self.out, "k_b.json")))

    def test_corrupted_pair_with_valid_json_is_rejected_before_restore(self):
        self.assertEqual(self.publish_valid()[1], "ok")
        good = self.seed_good_outputs()
        served = os.path.join(self.cas, "shader", "d1", "src", "k.res.json")
        value = json.load(open(served))
        value["cbufs"] = [{"binding": 0, "slot": 0}, {"binding": 3, "slot": 1}]   # 绑定漂移，JSON 合法、键/阶段不变
        write(served, json.dumps(value))
        hit, why = self.m.cache_lookup("k", "d1", "r1", "code")
        self.assertFalse(hit)
        self.assertIn("摘要不符", why)
        self.assert_outputs(good)   # 公共产物一个字节都没被覆盖 | not a byte of the public outputs was overwritten

    def test_revalidation_still_rejects_pair_with_refreshed_digest(self):
        self.assertEqual(self.publish_valid()[1], "ok")
        good = self.seed_good_outputs()
        served = os.path.join(self.cas, "shader", "d1", "src", "k.res.json")
        value = json.load(open(served))
        value["cbufs"] = [{"binding": 0, "slot": 0}, {"binding": 3, "slot": 1}]
        write(served, json.dumps(value))
        meta_path = os.path.join(self.cas, "index", "k.json")
        meta = json.load(open(meta_path))
        meta["pair"]["res"] = gate.file_sha(served)   # 摘要也改成新值：只能靠重跑契约拦住 | digest refreshed: only re-running the contract catches it
        write(meta_path, json.dumps(meta))
        hit, why = self.m.cache_lookup("k", "d1", "r1", "code")
        self.assertFalse(hit)
        self.assertIn("不再满足契约", why)
        self.assert_outputs(good)

    def test_corrupted_published_reflection_with_valid_labels_is_rejected(self):
        self.assertEqual(self.publish_valid()[1], "ok")
        good = self.seed_good_outputs()
        published = os.path.join(self.cas, "shader", "d1", "mac", "k_b.json")
        value = json.load(open(published))
        value["bindings"] = [{"kind": "cbuf", "reg": 99}]   # 标签（key/chain/stage/route）全对，内容被改
        write(published, json.dumps(value))
        hit, why = self.m.cache_lookup("k", "d1", "r1", "code")
        self.assertFalse(hit)
        self.assertIn("已发布产物", why)
        self.assert_outputs(good)

    def test_failed_rebuild_after_rejected_cache_keeps_good_outputs(self):
        self.assertEqual(self.publish_valid()[1], "ok")
        good = self.seed_good_outputs()
        write(os.path.join(self.cas, "shader", "d1", "src", "k.spv"), b"corrupted")
        hit, _ = self.m.cache_lookup("k", "d1", "r1", "code")
        self.assertFalse(hit)
        bad = dict(hand.VS_RES, cbufs=[{"binding": 0, "slot": 0}, {"binding": 5, "slot": 1}])
        self.pair(res=bad)
        r = self.m.fin("k")
        self.assertEqual(r[1], "契约不符")
        self.assert_outputs(good)
        self.assertEqual(self.m.final_exit([r]), 1)

    # —— 3. 缺省模式语义不变 | default mode keeps the old semantics ——————————————————————

    def test_default_mode_success_is_zero_and_failures_do_not_exit_nonzero(self):
        self.m.CONTRACT_ON = False
        self.m.xerr = {"k": "producer failed"}
        r = self.m.fin("k")
        self.assertEqual(r[1], "翻译失败")
        self.assertEqual(self.m.final_exit([r]), 0)

    def test_default_mode_restores_legacy_entry_without_gate_record(self):
        self.m.CONTRACT_ON = False
        root = os.path.join(self.cas, "shader", "d2", "mac")
        os.makedirs(root)
        write(os.path.join(root, "k_b.metallib"), b"L")
        write(os.path.join(root, "k_b.json"), json.dumps({"route": "B", "dm_ctl": "k", "chain": "tchain", "stage": "vs"}))
        os.makedirs(os.path.join(self.cas, "index"), exist_ok=True)
        write(os.path.join(self.cas, "index", "k.json"), json.dumps({"digest": "d2"}))
        hit, why = self.m.cache_lookup("k", "d2", "r1", "code")
        self.assertTrue(hit, why)


if __name__ == "__main__":
    unittest.main(verbosity=1)
