#!/usr/bin/env python3
# 同进程套件隔离的回归：两个构建测试套件按正序/反序只跑 setUpClass+tearDownClass，断言 sys.modules 恢复原状；
# 再诱导 gate 套件 setUpClass 中途失败，断言全局状态恢复且下一个套件仍能建起沙箱。
# Regression for in-process suite isolation: both build suites are set up/torn down in forward and reverse order with
# sys.modules asserted back to its previous state; then the gate suite's setUpClass is made to fail midway, asserting the
# globals are restored and the next suite can still build its sandbox.
import os, subprocess, sys, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import _sandbox_isolation as sandbox_iso
import test_build_finish_io as finish_io
import test_build_gate_paths as gate_paths


class Isolation(unittest.TestCase):
    def snapshot(self):
        return {name: sys.modules.get(name) for name in sandbox_iso.SANDBOX_MODULES}

    def assert_restored(self, before):
        for name, mod in before.items():
            self.assertIs(sys.modules.get(name), mod, name)

    def test_forward_and_reverse_order_restore_modules(self):
        for order in ((finish_io.FinishIO, gate_paths.BuildPaths), (gate_paths.BuildPaths, finish_io.FinishIO)):
            for cls in order:
                with self.subTest(suite=cls.__name__):
                    before = self.snapshot()
                    cls.setUpClass()
                    try:
                        self.assertTrue(os.path.exists(cls.m.dmsh_chain.HEADER))   # 本轮沙箱自己的替身 | this run's own stand-in
                    finally:
                        cls.tearDownClass()
                    # 沙箱目录删掉后，上一个套件的假 dmsh_chain 不得留在 sys.modules | no stale fake after the temp dir is gone
                    self.assert_restored(before)

    def test_failed_setup_class_restores_and_next_suite_builds(self):
        before = self.snapshot()
        real_copy = gate_paths.shutil.copy

        def boom(*a, **k):
            raise OSError("induced copy failure")

        gate_paths.shutil.copy = boom
        try:
            with self.assertRaises(OSError):
                gate_paths.BuildPaths.setUpClass()
        finally:
            gate_paths.shutil.copy = real_copy
        self.assert_restored(before)
        finish_io.FinishIO.setUpClass()   # 失败之后下一个套件仍可用 | the next suite still works after the failure
        try:
            self.assertTrue(os.path.exists(finish_io.FinishIO.m.dmsh_chain.HEADER))
        finally:
            finish_io.FinishIO.tearDownClass()


    def test_sys_path_and_subprocess_restored(self):
        before_path, before_run = list(sys.path), subprocess.run
        finish_io.FinishIO.setUpClass()
        finish_io.FinishIO.tearDownClass()
        self.assertEqual(sys.path, before_path)
        self.assertIs(subprocess.run, before_run)   # 不把导入期的外调替身留给主进程 | no import-time stand-in left in the main process


if __name__ == "__main__":
    unittest.main(verbosity=1)
