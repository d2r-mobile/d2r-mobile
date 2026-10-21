#!/usr/bin/env python3
# 构建测试沙箱的隔离夹具：快照 sys.modules 里的替身、sys.path、环境变量与 subprocess.run，
# 在 tearDownClass（以及 setUpClass 中途失败）时恢复。多个构建测试套件在同进程 unittest discover 下运行时，
# 前一套遗留的假 dmsh_chain（HEADER 指向已删除的临时目录）会让后一套 setUpClass 失败——这里只做恢复，不清空模块表。
# Isolation fixture for the build-test sandboxes: snapshots the stand-in entries in sys.modules, sys.path, the environment
# and subprocess.run, and restores them in tearDownClass (and when setUpClass fails midway). When several build suites run
# under one in-process unittest discover, the fake dmsh_chain left behind by the previous suite (its HEADER pointing into a
# deleted temp dir) breaks the next setUpClass; this only restores state and never clears the module table wholesale.
import os, subprocess, sys

# 沙箱会以这些名字导入替身或受测模块 | the sandbox imports stand-ins or tested modules under these names
SANDBOX_MODULES = ("dmsh_chain", "dmsh_facts", "dmsh_fetch", "dmsh_contract_gate", "dmsh_spirv_contract")
_MISSING = object()


class IsolatedSandbox:
    """调用 restore() 前可重复调用，幂等。 | restore() is idempotent."""

    def __init__(self, names=SANDBOX_MODULES):
        self._names = names
        self._modules = {n: sys.modules.get(n, _MISSING) for n in names}
        self._path = list(sys.path)
        self._env = dict(os.environ)
        self._run = subprocess.run
        self._restored = False

    def restore(self):
        if self._restored:
            return
        self._restored = True
        for name, mod in self._modules.items():
            if mod is _MISSING:
                sys.modules.pop(name, None)
            else:
                sys.modules[name] = mod
        sys.path[:] = self._path
        os.environ.clear()
        os.environ.update(self._env)
        subprocess.run = self._run

    def leaked_modules(self):
        """当前 sys.modules 里指向已不存在的文件（或本次快照之外新增）的沙箱替身，用于回归断言。
        Sandbox stand-ins currently in sys.modules that point at a deleted file or were added after the snapshot."""
        bad = []
        for name in self._names:
            mod = sys.modules.get(name)
            if mod is None:
                continue
            if self._modules.get(name, _MISSING) is not mod:
                bad.append(name)
            path = getattr(mod, "__file__", None)
            if path and not os.path.exists(path):
                bad.append(name)
        return bad
