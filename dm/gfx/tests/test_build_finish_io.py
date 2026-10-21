#!/usr/bin/env python3
# dmsh_build.py 真实 finish() 的 I/O 边界测试：不替换 finish，只把 subprocess 与 iOS 编译器换成受控桩，
# 验证「所有必需阶段成功后才提交、任一阶段失败旧产物一个字节不动、并发不同键互不干扰」。
# I/O-boundary tests for the real finish(): finish itself is NOT replaced - only subprocess and the iOS compiler are stubbed,
# checking "commit only after every required stage succeeds", "no byte of the old outputs changes on failure", and
# "concurrent distinct keys stay isolated".
import importlib.util, json, os, shutil, subprocess, sys, tempfile, types, unittest
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
REPO_TOOLS = os.path.abspath(os.path.join(HERE, "..", "tools"))
sys.path.insert(0, REPO_TOOLS)
sys.path.insert(0, HERE)
import dmsh_contract_gate as gate
import test_spirv_contract as hand
import _sandbox_isolation as sandbox_iso   # 同进程多套件运行时恢复 sys.modules/sys.path/环境 | restore sys.modules/sys.path/env between suites

MSL = """#include <metal_stdlib>
using namespace metal;
vertex float4 main0(uint vid [[vertex_id]]) {
    return float4(0.0);
}
"""
REAL_FINISH = None   # 沙箱模块里的真实 finish()（模块级，避免类属性绑定 self） | the real finish() (module level, no self binding)


def ios_compile_fail(*a, **k):
    raise subprocess.CalledProcessError(1, ["ios_metal.compile"])

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


def write(path, data, binary=False):
    with open(path, "wb" if binary or isinstance(data, (bytes, bytearray)) else "w") as f:
        f.write(data)


def read(path):
    with open(path, "rb") as f:
        return f.read()


def build_sandbox(root):
    tools = os.path.join(root, "tools")
    nvn = os.path.join(root, "nvn")
    os.makedirs(tools)
    os.makedirs(nvn)
    for name in ("dmsh_build.py", "dmsh_contract_gate.py", "dmsh_spirv_contract.py"):
        shutil.copy(os.path.join(REPO_TOOLS, name), os.path.join(tools, name))
    for name in ("shader_map.tsv", "shader_map.tsv.conflicts.tsv", "bf_want.tsv"):
        write(os.path.join(nvn, name), "")
    header = os.path.join(root, "dmsh_chain.h")
    write(header, '"tchain"\n')
    lock = os.path.join(root, "lock.txt")
    write(lock, "fake lock\n")
    write(os.path.join(tools, "dmsh_chain.py"), FAKE_CHAIN % {"header": header, "lock": lock})
    write(os.path.join(tools, "dmsh_facts.py"), 'def load():\n    return {"k": {}}\n')
    write(os.path.join(tools, "dmsh_fetch.py"), 'def variant(*a, **k):\n    return None, None\n')
    out = os.path.join(root, "outpublic", "b-tchain")
    os.makedirs(out)
    dm_dir = os.path.join(root, "dm")
    os.makedirs(dm_dir)
    old_argv, old_env = sys.argv[:], dict(os.environ)
    sys.argv = ["dmsh_build.py", dm_dir, os.path.dirname(out)]
    os.environ.update({"DMSH_CONTRACT": "0", "DM_ROOT": root, "J": "1"})
    os.environ.pop("IOSOUT", None)
    os.environ.pop("DMSH_CAS", None)
    real_run = subprocess.run

    def probe(cmd, *a, **k):
        if list(cmd[:2]) == ["spirv-cross", "--version"]:
            return types.SimpleNamespace(stdout="spirv-cross fake", stderr="", returncode=0)
        raise AssertionError("unexpected subprocess at import: %r" % (cmd,))

    subprocess.run = probe
    try:
        spec = importlib.util.spec_from_file_location("dmsh_build_finish_sandbox", os.path.join(tools, "dmsh_build.py"))
        mod = importlib.util.module_from_spec(spec)
        sys.path.insert(0, tools)
        try:
            spec.loader.exec_module(mod)
        finally:
            sys.path.remove(tools)
    finally:
        sys.argv = old_argv
        subprocess.run = real_run
        os.environ.clear()
        os.environ.update(old_env)
    return mod, out


class FinishIO(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._iso = sandbox_iso.IsolatedSandbox()   # 先快照：setUpClass 失败也要恢复 | snapshot first: restore even if setUpClass fails
        try:
            cls.tmp = tempfile.mkdtemp(prefix="dmshfin_")
            cls.root = cls.tmp
            cls.m, cls.out = build_sandbox(cls.tmp)
            cls.cas = os.path.join(cls.root, "cas")
            cls.w = os.path.join(cls.root, "work")
            os.makedirs(cls.cas)
            os.makedirs(cls.w)
            global REAL_FINISH
            REAL_FINISH = cls.m.finish   # 在装桩之前抓住真实实现 | grab the real implementation before stubbing
            cls._real_run = subprocess.run
        except BaseException:
            cls._iso.restore()
            shutil.rmtree(getattr(cls, "tmp", ""), ignore_errors=True)
            raise

    @classmethod
    def tearDownClass(cls):
        try:
            shutil.rmtree(cls.tmp, ignore_errors=True)
        finally:
            cls._iso.restore()   # 不把假 dmsh_chain 留给下一个套件 | never leave the fake dmsh_chain to the next suite

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
        m.IOSOUT = None
        m.loc = {"k": ("pkg.bin", 0), "k2": ("pkg.bin", 1)}
        m.pc_of = {"k": "0x1", "k2": "0x2"}
        m.jobs = {"k": {"stage": "vs"}, "k2": {"stage": "vs"}}
        m.c1s = {"k": b"", "k2": b""}
        m.finish = REAL_FINISH
        m.fetch_finish = lambda key, src, refl: refl
        self.ios = os.path.join(self.root, "iosout")
        shutil.rmtree(self.ios, ignore_errors=True)
        os.makedirs(self.ios)

    def tearDown(self):
        self.m.subprocess = subprocess

    # —— 受控 I/O | controlled I/O ————————————————————————————————————————————————

    def install_run(self, fail=None):
        real_cpe = subprocess.CalledProcessError

        def fake(cmd, *a, **k):
            cmd = list(cmd)
            if cmd[:2] == ["cp", "-c"]:   # clone() 的快速路径 | clone()'s fast path
                shutil.copyfile(cmd[2], cmd[3])
                return types.SimpleNamespace(returncode=0, stdout=b"", stderr=b"")
            if cmd and cmd[0] == "spirv-cross":
                write(cmd[cmd.index("--output") + 1], MSL)
                return types.SimpleNamespace(returncode=0, stdout="", stderr="")
            if cmd[:2] == ["xcrun", "-sdk"]:
                if fail == "metallib" and "metallib" in cmd:
                    raise real_cpe(1, cmd)
                if "metallib" in cmd:
                    write(cmd[cmd.index("-o") + 1], ("LIB:" + os.path.basename(cmd[4])).encode())
                else:
                    write(cmd[cmd.index("-o") + 1], b"AIR")
                return types.SimpleNamespace(returncode=0, stdout="", stderr="")
            raise AssertionError("unexpected subprocess: %r" % (cmd,))

        self.m.subprocess = types.SimpleNamespace(run=fake, CalledProcessError=real_cpe,
                                                 DEVNULL=subprocess.DEVNULL, PIPE=subprocess.PIPE)

    def prepare_pair(self, key):
        write(os.path.join(self.w, key + ".res.json"), json.dumps(dict(hand.VS_RES, key=key)))
        write(os.path.join(self.w, key + ".spv"), hand.words_of(hand.module(hand.vertex_module())), binary=True)

    def seed_good(self):
        good = {}
        for name in ("k_b.metallib", "k_b.metal", "k_b.json"):
            write(os.path.join(self.out, name), b"GOOD-" + name.encode())
            good[name] = read(os.path.join(self.out, name))
        return good

    def assert_good(self, good):
        for name, data in good.items():
            self.assertEqual(read(os.path.join(self.out, name)), data, name)

    def tmp_leftovers(self, *dirs):
        bad = []
        for d in dirs:
            if os.path.isdir(d):
                bad += [n for n in os.listdir(d) if ".tmp-" in n]
        return bad

    # —— 成功：完整新产物 | success: the complete new set ————————————————————————————————

    def test_success_publishes_complete_set(self):
        self.prepare_pair("k")
        self.install_run()
        r = self.m.finish("k", self.w, self.m.jobs["k"], b"")
        self.assertEqual(r[0], "ok", r)
        for name in ("k_b.metallib", "k_b.metal", "k_b.json"):
            self.assertTrue(os.path.exists(os.path.join(self.out, name)), name)
        refl = json.load(open(os.path.join(self.out, "k_b.json")))
        self.assertEqual(refl["dm_ctl"], "k")
        self.assertEqual(refl["chain_metal"], self.m.MAC_RECORD)
        self.assertEqual(self.tmp_leftovers(self.out), [])

    # —— 后续阶段失败：旧产物不动 | later-stage failure: old outputs untouched ————————————————

    def test_ios_compile_failure_preserves_old_mac_outputs(self):
        good = self.seed_good()
        self.m.IOSOUT = self.ios
        self.m.ios_metal = types.SimpleNamespace(RECORD="iosrec", compile=ios_compile_fail)
        self.prepare_pair("k")
        self.install_run()
        r = self.m.finish("k", self.w, self.m.jobs["k"], b"")
        self.assertEqual(r[0], "iOS metal 失败", r)
        self.assert_good(good)   # 旧 mac 产物逐字节不变 | old mac outputs are byte-identical
        self.assertEqual(os.listdir(self.ios), [])
        self.assertEqual(self.tmp_leftovers(self.out, self.ios), [])

    def test_metallib_failure_preserves_old_outputs(self):
        good = self.seed_good()
        self.prepare_pair("k")
        self.install_run(fail="metallib")
        r = self.m.finish("k", self.w, self.m.jobs["k"], b"")
        self.assertEqual(r[0], "metal 失败", r)
        self.assert_good(good)
        self.assertEqual(self.tmp_leftovers(self.out), [])

    def test_ios_failure_does_not_publish_any_new_file(self):
        self.m.IOSOUT = self.ios
        self.m.ios_metal = types.SimpleNamespace(RECORD="iosrec", compile=ios_compile_fail)
        self.prepare_pair("k")
        self.install_run()
        self.m.finish("k", self.w, self.m.jobs["k"], b"")
        for name in ("k_b.metallib", "k_b.metal", "k_b.json"):
            self.assertFalse(os.path.exists(os.path.join(self.out, name)), name)

    # —— 并发不同键隔离 | concurrent distinct keys stay isolated ————————————————————————

    def test_concurrent_keys_are_isolated(self):
        self.prepare_pair("k")
        self.prepare_pair("k2")
        self.install_run()
        with ThreadPoolExecutor(2) as ex:
            results = list(ex.map(lambda key: self.m.finish(key, self.w, self.m.jobs[key], b""), ["k", "k2"]))
        self.assertEqual([r[0] for r in results], ["ok", "ok"])
        libs = {}
        for key in ("k", "k2"):
            libs[key] = read(os.path.join(self.out, key + "_b.metallib"))
            refl = json.load(open(os.path.join(self.out, key + "_b.json")))
            self.assertEqual(refl["dm_ctl"], key)
        self.assertIn(b"k.air", libs["k"])
        self.assertIn(b"k2.air", libs["k2"])
        self.assertNotEqual(libs["k"], libs["k2"])
        self.assertEqual(self.tmp_leftovers(self.out), [])


if __name__ == "__main__":
    unittest.main(verbosity=1)
