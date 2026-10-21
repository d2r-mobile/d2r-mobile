#!/usr/bin/env python3
"""Owned, no-game fixtures for --reuse shader identity validation."""
import contextlib
import hashlib
import io
import json
import sys
import tempfile
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
import make_package


def make_tree(root, subgroup=64):
    for name in ("ExeFS", "RomFS", "game", "shaders/spirv", "shaders/work"):
        (root / name).mkdir(parents=True, exist_ok=True)
    config = {
        "precision": "full",
        "host_subgroup_size": subgroup,
        "guest_warp_size": 32,
        "shader_host_capabilities": {"shader_float64": False, "fragment_shader_interlock": False},
        "jobs_sha256": "a" * 64,
    }
    profile = dict(config, schema=1, shader_count=1, spirv_target="vulkan1.1", spirv_validated=False)
    (root / "shaders/work/build-config.json").write_text(json.dumps(config) + "\n")
    (root / "shaders/spirv/host-profile.json").write_text(json.dumps(profile) + "\n")
    (root / "shaders/spirv/host-subgroup-size.txt").write_text(str(subgroup) + "\n")
    return root


def source_bytes(root):
    return {str(path.relative_to(root)): hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(root.rglob("*")) if path.is_file()}


def run_main(reuse, workspace, output, subgroup):
    old = sys.argv
    sys.argv = ["make_package.py", "game", "keys", "--reuse", str(reuse),
                "--workspace", str(workspace), "--output", str(output), "--subgroup", str(subgroup)]
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            make_package.main()
        return 0
    except SystemExit as error:
        return error.code
    finally:
        sys.argv = old


def main():
    original_derived, original_run = make_package.derived_tables, make_package.run
    with tempfile.TemporaryDirectory(prefix="dm-reuse-profile-") as temporary:
        base = Path(temporary)
        success_reuse = make_tree(base / "success")
        success_work = base / "success-work"
        success_output = success_work / "package.zip"
        success_work.mkdir()
        success_output.write_bytes(b"package")
        before = source_bytes(success_reuse)
        calls = []
        make_package.derived_tables = lambda romfs, workspace: (calls.append(("derived", Path(workspace))) or Path(workspace) / "derived.tsv")
        make_package.run = lambda command, label, env=None, timeout=None: (calls.append(("run", label)) or "")
        code = run_main(success_reuse, success_work, success_output, 64)
        assert code == 0, code
        assert ("derived", success_work / "reuse-derived") in calls
        assert any(kind == "run" for kind, _ in calls)
        assert source_bytes(success_reuse) == before, "matching reuse changed source cache"

        def failure(name, mutate, requested=64):
            root = make_tree(base / name)
            mutate(root)
            before_case = source_bytes(root)
            calls.clear()
            code = run_main(root, base / (name + "-work"), base / (name + "-work") / "package.zip", requested)
            assert code != 0, name
            assert not calls, (name, calls)
            assert source_bytes(root) == before_case, name + " changed source cache"

        failure("mismatch", lambda root: None, 32)
        failure("missing-profile", lambda root: (root / "shaders/spirv/host-profile.json").unlink())
        failure("malformed-profile", lambda root: (root / "shaders/spirv/host-profile.json").write_text("{bad\n"))
        failure("missing-config", lambda root: (root / "shaders/work/build-config.json").unlink())
        failure("malformed-config", lambda root: (root / "shaders/work/build-config.json").write_text("{bad\n"))
        failure("malformed-subgroup", lambda root: (root / "shaders/spirv/host-subgroup-size.txt").write_text("64x\n"))
        failure("metadata-conflict", lambda root: (root / "shaders/work/build-config.json").write_text(
            json.dumps({"precision": "full", "host_subgroup_size": 32, "guest_warp_size": 32,
                        "shader_host_capabilities": {"shader_float64": False, "fragment_shader_interlock": False},
                        "jobs_sha256": "a" * 64}) + "\n"))
    make_package.derived_tables, make_package.run = original_derived, original_run
    print("PASS: reuse shader profile matching, mismatch, missing/malformed, conflict; source bytes unchanged")


if __name__ == "__main__":
    main()
