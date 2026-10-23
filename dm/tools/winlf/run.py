#!/usr/bin/env python3
"""保留既有资源读取适配器，再以 LF 文本写出运行生成器。
Keep an existing read adapter, then run a generator with LF text output.
from pathlib import Path
import runpy
import sys
"""
from pathlib import Path
import runpy
import sys

if len(sys.argv) < 2:
    raise SystemExit("usage: run.py generator.py [arguments]")
# Python 启动时已加载用户的 sitecustomize；LF 包装叠在它上面，不遮掉只读资源映射。
# Startup already loaded the user's sitecustomize; layer LF writes over it without hiding resource reads.
runpy.run_path(str(Path(__file__).with_name('sitecustomize.py')))
script = str(Path(sys.argv[1]).resolve())
sys.argv = [script] + sys.argv[2:]
sys.path.insert(0, str(Path(script).parent))
runpy.run_path(script, run_name='__main__')
