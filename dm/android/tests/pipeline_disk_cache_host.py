#!/usr/bin/env python3
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="dm-pipeline-cache-") as temporary:
    output = Path(temporary) / "test"
    subprocess.run([
        sys.argv[2] if len(sys.argv) > 2 else "clang", "-std=c11", "-Wall", "-Wextra", "-Werror",
        "-fsanitize=address,undefined", "-pthread", "-I", sys.argv[1], "-I", str(root / "native"),
        str(root / "tests/pipeline_disk_cache_test.c"), str(root / "native/legacy_digest.c"),
        "-o", str(output),
    ], check=True)
    subprocess.run([str(output), temporary], check=True)
