#!/usr/bin/env python3
"""原版 CASC 着色器及 main 内嵌着色器 → Vulkan SPIR-V 和资源反射。
Original CASC and main-embedded shaders → Vulkan SPIR-V and resource reflection.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import struct
import subprocess
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "verify"), str(ROOT / "gfx/tools")]
import casc_overlay
import dmsh_facts


def extract_packages(romfs, output):
    output.mkdir(parents=True, exist_ok=True)
    count = 0
    for logical, row in casc_overlay.read_manifest(romfs).items():
        if not logical.startswith("data/hd/shaders/") or not logical.endswith(".bin"):
            continue
        key = bytes.fromhex(row[6])
        path = romfs / "data" / f"{key[9]:02d}-{key[10]:08d}.data"
        with path.open("rb") as f:
            f.seek(int.from_bytes(key[11:16], "big")); raw = f.read(int(row[7]))
        if row[8] == "z": content = zlib.decompress(raw)
        elif row[8] == "n": content = raw
        elif raw.startswith(b"BLTE"): content = casc_overlay.read_data_file(romfs / "data", key, int(row[7]))
        else: raise ValueError(f"Unsupported shader encoding {row[8]} for {logical}")
        if len(content) != int(row[5]): raise ValueError(f"Truncated shader package: {logical}")
        (output / Path(logical).name).write_bytes(content); count += 1
    return count


def prepare_jobs(packages, main, work, reduced=False, host_subgroup_size=32):
    import nsoimg
    facts = dmsh_facts.load(); records = {}
    for path in sorted(packages.glob("*.bin")):
        data = path.read_bytes(); starts = [m.start() for m in re.finditer(b"BTCDBTCD", data)]
        for i, start in enumerate(starts):
            record = data[start:starts[i + 1] if i + 1 < len(starts) else len(data)]
            ci = record.find(bytes.fromhex("34127698"))
            if ci < 0: raise ValueError(f"Missing shader control segment: {path}:{i}")
            control_size = struct.unpack_from("<I", record, 0x28)[0]
            control = record[ci:ci + control_size]
            key = hashlib.sha256(control).hexdigest()[:16]
            records.setdefault(key, (path.name, path.name[:2], record[0x38:ci], control))
    image = nsoimg.load(main)
    for line in (ROOT / "gfx/nvn/shader_map.tsv").read_text().splitlines():
        cols = line.split("\t")
        if len(cols) <= 4 or not cols[0].startswith("main.rodata@"): continue
        offset = int(cols[0].split("@")[1], 16)
        start = image.rfind(bytes.fromhex("78563412"), offset - 0x1000, offset)
        if start < 0: raise ValueError("Embedded shader code is absent")
        records.setdefault(cols[3], (cols[0], {"顶点": "vs", "像素": "ps"}[cols[2]], image[start:offset], image[offset:offset + 0x880]))
    # 原 main 内的四个计数器计算核没有 PC 配对，线程组是 32×1×1。
    # The four counter compute kernels embedded in main have no PC pair and use 32×1×1 workgroups.
    for offset in (0x1df10e8, 0x1df1a94, 0x1df2440, 0x1df2dec):
        start = image.rfind(bytes.fromhex("78563412"), offset - 0x1000, offset)
        control = image[offset:offset + 0x880]
        # 这些内嵌计算核只有原始指令，没有包内的 0x12345678 代码头。
        # These embedded compute kernels contain raw instructions without the package's 0x12345678 code header.
        if start < 0: start = offset - struct.unpack_from("<I", control, 0x704)[0]
        if start < 0 or control[:4] != bytes.fromhex("34127698"): raise ValueError("Embedded counter compute shader is absent")
        key = hashlib.sha256(control).hexdigest()[:16]
        records.setdefault(key, (f"main.counter@{offset:x}", "cs", image[start:offset], control))
    work.mkdir(parents=True, exist_ok=True); jobs = []
    for key, (name, stage, code, control) in sorted(records.items()):
        c1_start, c1_end = struct.unpack_from("<II", control, 0x700)
        if not 0 <= c1_start <= c1_end <= len(code): raise ValueError(f"Invalid constant segment: {key}")
        c1 = code[c1_start:c1_end]
        path = work / (key + ".bin"); path.write_bytes(code if stage == "cs" else code[0x30:])
        job = {"key": key, "stage": stage, "code": str(path), "c1": c1.hex(), "debug": False, "reduced": reduced, "host_subgroup_size": host_subgroup_size, "set": {"vs":0,"ps":1,"gs":2,"hs":3,"ds":4,"cs":0}[stage]}
        job.update(facts.get(key, {})); jobs.append(job)
        if name.startswith("main.counter@"): job.update(local=[32, 1, 1], shared=0)
    (work / "jobs.json").write_text(json.dumps(jobs))
    return jobs


def reuse_jobs(source, reduced, host_subgroup_size=32):
    """复用已经提取的机器码与事实，新选项只写到新工作目录。
    Reuse extracted machine code and facts; write new options only to the destination work directory.
    """
    source = source.resolve(); jobs = json.loads(source.read_text()); keys = set()
    if not isinstance(jobs, list) or not jobs: raise ValueError("The source jobs file is empty or is not an array")
    for job in jobs:
        key = job["key"]
        if not re.fullmatch(r"[0-9a-f]{16}", key) or key in keys: raise ValueError(f"Invalid or duplicate shader key: {key}")
        keys.add(key); code = Path(job["code"])
        if not code.is_absolute(): code = source.parent / code
        code = code.resolve()
        if not code.is_file(): raise ValueError(f"Missing extracted shader code: {code}")
        job.update(code=str(code), reduced=reduced, host_subgroup_size=host_subgroup_size)
    return jobs


def android_shader_capabilities():
    # 与Android实际device启用政策一致；原Metal和Ryujinx缺省不修改。
    # Match the Android device enable policy; preserve the original Metal and Ryujinx defaults.
    return {"shader_float64": False, "fragment_shader_interlock": False}


def validate_translator_profile(tool, host_subgroup_size):
    profile_path = tool.parent / "translation-api.json"
    if not profile_path.is_file(): raise ValueError("The Android translator has no capability profile; build a new translator")
    profile = json.loads(profile_path.read_text())
    if profile.get("shader_host_capabilities") != android_shader_capabilities():
        raise ValueError("The Android translator does not match the enabled shader capabilities; build a new translator")
    if profile.get("translator_sha256") != hashlib.sha256(tool.read_bytes()).hexdigest():
        raise ValueError("The Android translator profile does not identify this binary; build a new translator")
    if host_subgroup_size != 32 and not profile.get("host_subgroup_size_from_job"):
        raise ValueError("The Android translator does not declare host-subgroup job support; build a new translator")


def record_build_config(work, output, jobs, precision, tool=None):
    # 原版 key 仅识别客体着色器，不识别翻译选项；阻止同目录误用另一精度的旧产物。
    # Guest shader keys do not identify translation options; prevent old output from another precision being reused.
    subgroup_sizes = {job.get("host_subgroup_size", 32) for job in jobs}
    if len(subgroup_sizes) != 1: raise ValueError("Mixed host subgroup sizes in one shader cache")
    config = {"precision": precision, "host_subgroup_size": next(iter(subgroup_sizes)), "guest_warp_size": 32, "shader_host_capabilities": android_shader_capabilities(), "jobs_sha256": hashlib.sha256(json.dumps(jobs, sort_keys=True).encode()).hexdigest()}
    if tool is not None: config["translator_sha256"] = hashlib.sha256(tool.read_bytes()).hexdigest()
    path = work / "build-config.json"
    previous = json.loads(path.read_text()) if path.exists() else None
    populated = output.exists() and any(output.glob("*.spv"))
    if populated:
        # 不给既有缓存补写虚假的新能力政策；无记录或旧政策必须重译到独立目录。
        # Never label an existing cache with a new capability policy; missing/old profiles require a separate rebuild.
        if previous is None or previous.get("shader_host_capabilities") != config["shader_host_capabilities"]:
            raise ValueError("Existing SPIR-V has no matching Android capability record; select a separate output directory")
        if previous is not None:
            for key, value in config.items():
                if key in previous and previous[key] != value: raise ValueError(f"Existing SPIR-V build differs in {key}; select a separate output directory")
    if previous is not None and previous.get("shader_host_capabilities") == config["shader_host_capabilities"] and tool is None and "translator_sha256" in previous:
        config["translator_sha256"] = previous["translator_sha256"]
    path.write_text(json.dumps(config, indent=2) + "\n")


def storage_image_capabilities(raw):
    """仅为实际 Unknown-format storage image 访问补声明；不修改任何执行指令或 ID。
    Add declarations only for actual Unknown-format storage image accesses; preserve executable instructions and IDs.
    """
    if len(raw) < 20 or len(raw) % 4: raise ValueError("Truncated SPIR-V module")
    words = struct.unpack("<" + str(len(raw) // 4) + "I", raw)
    if words[0] != 0x07230203: raise ValueError("Invalid SPIR-V magic")
    instructions = []; images = {}; capabilities = set(); position = 5
    while position < len(words):
        length, opcode = words[position] >> 16, words[position] & 65535
        if not length or position + length > len(words): raise ValueError("Invalid SPIR-V instruction")
        instructions.append((position, length, opcode))
        if opcode == 17:  # OpCapability
            if length != 2: raise ValueError("Invalid OpCapability")
            capabilities.add(words[position + 1])
        elif opcode == 25:  # OpTypeImage: result, sampled type, dim, depth, arrayed, MS, sampled, format
            if length not in (9, 10): raise ValueError("Invalid OpTypeImage")
            images[words[position + 1]] = (words[position + 3], words[position + 7], words[position + 8])
        position += length
    if 6 in capabilities: return raw, ()  # Kernel explicitly permits Unknown-format image access.
    # Khronos grammar gives these image-value producers the ResultType/ResultId prefix.
    # Undef, FunctionParameter/Call, Load, CompositeConstruct/Extract/Insert, CopyObject,
    # Image, Select, Phi and CopyLogical. Unknown producers fail rather than guessing an ID's type.
    typed_image_producers = {1, 55, 57, 61, 80, 81, 82, 83, 100, 169, 245, 400}
    value_types = {}
    for position, length, opcode in instructions:
        if opcode in typed_image_producers:
            if length < 3: raise ValueError("Invalid typed SPIR-V result")
            value_types[words[position + 2]] = words[position + 1]
    required = set()
    for position, length, opcode in instructions:
        if opcode == 99:  # OpImageWrite has no result prefix.
            if length < 4: raise ValueError("Invalid OpImageWrite")
            image = words[position + 1]; capability = 56
        elif opcode in (98, 320):  # OpImageRead / OpImageSparseRead: ResultType, ResultId, Image
            if length < 5: raise ValueError("Invalid OpImageRead/OpImageSparseRead")
            image = words[position + 3]; capability = 55
        else: continue
        image_type = images.get(value_types.get(image))
        if image_type is None: raise ValueError(f"Cannot resolve image object %{image} used by opcode {opcode}")
        dimension, sampled, image_format = image_type
        if image_format != 0: continue
        if capability == 55 and dimension == 6: continue  # SubpassData reads are explicitly exempt.
        if sampled not in (0, 2): raise ValueError("Storage image operation uses a sampled-only image type")
        required.add(capability)
    missing = sorted(required - capabilities)
    if not missing: return raw, ()
    # Logical layout requires capabilities before extensions/imports/memory model.
    insert = 5
    for position, length, opcode in instructions:
        if opcode != 17: break
        insert = position + length
    declarations = b"".join(struct.pack("<2I", (2 << 16) | 17, capability) for capability in missing)
    return raw[:insert * 4] + declarations + raw[insert * 4:], tuple(missing)


def postprocess_spirv(paths, backup, validator=None, workers=4):
    report = []
    for path in paths:
        raw = path.read_bytes(); fixed, added = storage_image_capabilities(raw)
        if not added: continue
        digest = hashlib.sha256(raw).hexdigest(); backup.mkdir(parents=True, exist_ok=True)
        saved = backup / (path.stem + "." + digest[:16] + ".spv")
        if saved.exists():
            if saved.read_bytes() != raw: raise ValueError(f"Different original SPIR-V at {saved}")
        else: saved.write_bytes(raw)
        temporary = path.with_suffix(".spv.cap.tmp"); temporary.write_bytes(fixed); os.replace(temporary, path)
        report.append({"key": path.stem, "added": list(added), "original_sha256": digest, "fixed_sha256": hashlib.sha256(fixed).hexdigest(), "backup": str(saved)})
    print(f"SPIR-V storage-image capability declarations repaired={len(report)}/{len(paths)}", flush=True)
    if report: (backup / "capability-report.json").write_text(json.dumps(report, indent=2) + "\n")
    if validator is not None:
        def validate(path):
            result = subprocess.run([str(validator), "--target-env", "vulkan1.1", str(path)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            return None if result.returncode == 0 else (path.name, result.stdout)
        with ThreadPoolExecutor(max_workers=workers) as pool: errors = [error for error in pool.map(validate, paths) if error is not None]
        print(f"SPIR-V Vulkan 1.1 validation={len(paths) - len(errors)}/{len(paths)}", flush=True)
        for name, error in errors: print(f"{name}: {error}", file=sys.stderr)
        if errors: raise ValueError(f"SPIR-V validation failed for {len(errors)} shaders")
    return report


def translator(dotnet, work):
    project = work / "translator"; project.mkdir(parents=True, exist_ok=True)
    source = (ROOT / "gfx/tools/dmsh_xlate/Program.cs").read_text()
    # 每阶段独立描述符集避免顶点/片元绑定号碰撞；原 Apple 翻译器不修改。
    # Separate descriptor sets per stage avoid vertex/fragment binding collisions; leave the Apple translator unchanged.
    source = source.replace("readonly byte[] _c1;", "readonly byte[] _c1; readonly int _set;")
    source = source.replace("_code = code; _job = job;", '_code = code; _job = job; _set = job["set"] is JsonNode s ? (int)s : 0;')
    source = source.replace("new(0, _binding++)", "new(_set, _binding++)").replace("new(0, b)", "new(_set, b)")
    # 原 Maxwell warp=32；Ryujinx 按真实 host subgroup 适配高半线程、SHFL 和 ballot/vote。
    # Maxwell warps remain 32; Ryujinx adapts upper host lanes, SHFL and ballot/vote to the actual host subgroup.
    anchor = "        public bool QueryHostSupportsTextureShadowLod() => true;"
    if source.count(anchor) != 1: raise ValueError("Android host-subgroup accessor injection anchor changed")
    # 离线生成器只能声明Android真正启用的能力，不能继承桌面缺省的无用double/interlock声明。
    # Declare only capabilities enabled by Android; do not inherit unused desktop double/interlock declarations.
    source = source.replace(anchor, '        public int QueryHostSubgroupSize() => _job["host_subgroup_size"] is JsonNode n ? (int)n : 32;\n'
        '        public bool QueryHostSupportsShaderFloat64() => false;\n'
        '        public bool QueryHostSupportsFragmentShaderInterlock() => false;\n' + anchor)
    (project / "Program.cs").write_text(source)
    ryu = ROOT / "third_party/ryujinx"
    csproj = f'''<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><AllowUnsafeBlocks>true</AllowUnsafeBlocks></PropertyGroup><ItemGroup><ProjectReference Include="{ryu}/src/Ryujinx.Graphics.Shader/Ryujinx.Graphics.Shader.csproj"/><ProjectReference Include="{ryu}/src/Ryujinx.Common/Ryujinx.Common.csproj"/><EmbeddedResource Include="Program.cs" LogicalName="src0"/><EmbeddedResource Include="dmsh_android.csproj" LogicalName="src1"/></ItemGroup></Project>'''
    (project / "dmsh_android.csproj").write_text(csproj)
    dest = work / "translator-bin"
    subprocess.run([str(dotnet), "publish", str(project / "dmsh_android.csproj"), "-c", "Release", "-o", str(dest)], check=True)
    (dest / "translation-api.json").write_text(json.dumps({"schema": 2, "host_subgroup_size_from_job": True, "guest_warp_size": 32, "shader_host_capabilities": android_shader_capabilities(), "translator_sha256": hashlib.sha256((dest / "dmsh_android.dll").read_bytes()).hexdigest()}, indent=2) + "\n")
    return dest / "dmsh_android.dll"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("romfs", type=Path, nargs="?"); parser.add_argument("main", type=Path, nargs="?"); parser.add_argument("output", type=Path)
    parser.add_argument("--dotnet", type=Path, default=Path.home() / "dm-work/tools/dotnet/dotnet")
    parser.add_argument("--prepare-only", action="store_true"); parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--precision", choices=("full", "reduced"), default="full", help="GPU contraction policy; use separate output directories for each variant (default: full)")
    parser.add_argument("--host-subgroup-size", type=int, choices=(32, 64, 128), default=32, help="Actual Vulkan subgroup size; preserve guest 32-lane warp semantics using Ryujinx adapters (default: 32)")
    parser.add_argument("--reuse-jobs", type=Path, help="Existing work/jobs.json; skip CASC/NSO extraction (romfs and main may be omitted)")
    parser.add_argument("--translator", type=Path, help="Reuse an existing Android dmsh_android.dll instead of building the translator")
    parser.add_argument("--postprocess-only", action="store_true", help="Repair required storage-image capability declarations in output/spirv without extraction/translation/reflection changes")
    parser.add_argument("--backup", type=Path, help="Original modified SPIR-V backup directory (default: adjacent shader-capability-backups/output-name)")
    parser.add_argument("--validate", action="store_true", help="Validate every SPIR-V module for Vulkan 1.1; fail on any validation error")
    parser.add_argument("--spirv-val", type=Path, default=Path.home() / "dm-work/tools/android/ndk/shader-tools/linux-x86_64/spirv-val")
    args = parser.parse_args()
    if args.jobs < 1: parser.error("--jobs must be positive")
    args.output = args.output.resolve(); work = args.output / "work"; packages = work / "packages"; output = args.output / "spirv"
    backup = args.backup.resolve() if args.backup is not None else args.output.parent / "shader-capability-backups" / args.output.name
    if backup.is_relative_to(args.output): parser.error("--backup must be outside the shader output directory")
    validator = args.spirv_val.resolve() if args.validate else None
    if validator is not None and not validator.is_file(): parser.error(f"Missing spirv-val executable: {validator}")
    if args.postprocess_only:
        if args.romfs is not None or args.main is not None or args.reuse_jobs is not None or args.translator is not None or args.prepare_only:
            parser.error("--postprocess-only accepts just output, backup and validation options")
        paths = sorted(output.glob("*.spv"))
        if not paths: parser.error(f"No SPIR-V modules in {output}")
        postprocess_spirv(paths, backup, validator, args.jobs); return
    if args.reuse_jobs is None and (args.romfs is None or args.main is None): parser.error("romfs and main are required unless --reuse-jobs is provided")
    reduced = args.precision == "reduced"
    if args.reuse_jobs is not None:
        if args.reuse_jobs.resolve() == (work / "jobs.json").resolve(): parser.error("--reuse-jobs must refer to a separate source work directory")
        jobs = reuse_jobs(args.reuse_jobs, reduced, args.host_subgroup_size);work.mkdir(parents=True, exist_ok=True)
        print(f"Reused shader jobs={len(jobs)} precision={args.precision} host_subgroup={args.host_subgroup_size} guest_warp=32", flush=True)
    else:
        n = extract_packages(args.romfs, packages); jobs = prepare_jobs(packages, args.main, work, reduced, args.host_subgroup_size)
        print(f"Shader packages={n} unique jobs={len(jobs)} precision={args.precision} host_subgroup={args.host_subgroup_size} guest_warp=32", flush=True)
    record_build_config(work, output, jobs, args.precision)
    if args.reuse_jobs is not None: (work / "jobs.json").write_text(json.dumps(jobs))
    if args.prepare_only: return
    tool = args.translator.resolve() if args.translator is not None else translator(args.dotnet, work)
    if not tool.is_file(): raise ValueError(f"Missing Android translator: {tool}")
    validate_translator_profile(tool, args.host_subgroup_size)
    record_build_config(work, output, jobs, args.precision, tool); output.mkdir(parents=True, exist_ok=True)
    def batch(index):
        subset = [j for j in jobs[index::args.jobs] if not (output / (j["key"] + ".spv")).exists() or not (output / (j["key"] + ".res.json")).exists()]
        path = work / f"jobs-{index}.json"; path.write_text(json.dumps(subset))
        with (work / f"translation-{index}.log").open("w") as log:
            result = subprocess.run([str(args.dotnet), str(tool), str(path), str(output)], stdout=log, stderr=log)
        retry = [dict(j, debug=True) for j in subset if not (output / (j["key"] + ".spv")).exists()]
        if retry:
            path.write_text(json.dumps(retry))
            with (work / f"translation-retry-{index}.log").open("w") as log:
                result = subprocess.run([str(args.dotnet), str(tool), str(path), str(output)], stdout=log, stderr=log)
        return result.returncode
    with ThreadPoolExecutor(max_workers=args.jobs) as pool: results = list(pool.map(batch, range(args.jobs)))
    postprocess_spirv([output / (j["key"] + ".spv") for j in jobs if (output / (j["key"] + ".spv")).is_file()], backup, validator, args.jobs)
    good = 0
    for job in jobs:
        path = output / (job["key"] + ".res.json")
        if not path.exists(): continue
        meta = json.loads(path.read_text()); meta.update({k: v for k, v in job.items() if k in ("c1", "set", "local", "attr", "fout", "shared")})
        path.write_text(json.dumps(meta, indent=1))
        # 紧凑二进制反射供 C 回放层读取，不在手机上解析 JSON。
        # Compact binary reflection for the C executor; no JSON parsing on the device.
        entries = [(0, {"binding": 0})]
        for kind, label in [(1, "cbufs"), (2, "sbufs"), (3, "textures"), (4, "images")]:
            entries += [(kind, b) for b in meta[label]]
        c1 = bytes.fromhex(meta.get("c1", "")); attrs = [0] * 32
        # PC 查询表只提供分量类型；实际输入位置由 SPIR-V 决定，不能绑定旧管线的无用流。
        # The PC fact table supplies component types only; SPIR-V determines actual inputs, avoiding unused streams left by older pipelines.
        if job["stage"] == "vs":
            code = (output / (job["key"] + ".spv")).read_bytes(); words = struct.unpack("<" + "I" * (len(code) // 4), code)
            locations, inputs, position = {}, set(), 5
            while position < len(words):
                length, opcode = words[position] >> 16, words[position] & 65535
                if not length or position + length > len(words): raise ValueError("Invalid SPIR-V instruction")
                if opcode == 71 and length >= 4 and words[position + 2] == 30: locations[words[position + 1]] = words[position + 3]
                elif opcode == 59 and length >= 4 and words[position + 3] == 1: inputs.add(words[position + 2])
                position += length
            for variable in inputs:
                location = locations.get(variable)
                if location is not None and location < 32:
                    attrs[location] = {"float": 1, "sint": 2, "uint": 3}[meta.get("attr", {}).get(str(location), "float")]
        binary = struct.pack("<4s6I", b"DMVK", 1, {"vs":0,"ps":1,"gs":2,"hs":3,"ds":4,"cs":5}[job["stage"]], meta["set"], len(entries), len(c1), 32)
        for kind, b in entries:
            values = [kind, b["binding"], b.get("slot", 0), b.get("cb_slot", b.get("cbuf_slot", 0)), b.get("cb_offset", 0), b.get("handle", 0), b.get("type", 0), b.get("array", 1), b.get("flags", 0), b.get("separate", 0)]
            binary += struct.pack("<10I", *(v & 0xffffffff for v in values))
        (output / (job["key"] + ".meta")).write_bytes(binary + c1 + struct.pack("<32I", *attrs)); good += 1
    print(f"SPIR-V reflection pairs={good}/{len(jobs)}", flush=True)
    if any(results) or good != len(jobs): sys.exit(1)
    # 将 host profile 随缓存一起保存，设备安装/对照时可明确区分32与64版本。
    # Save the host profile beside the cache so device installation/comparisons can distinguish 32/64 variants.
    profile = json.loads((work / "build-config.json").read_text()); profile.update(schema=1, shader_count=good, spirv_target="vulkan1.1", spirv_validated=bool(validator))
    (output / "host-profile.json").write_text(json.dumps(profile, indent=2) + "\n")
    (output / "host-subgroup-size.txt").write_text(str(args.host_subgroup_size) + "\n")


if __name__ == "__main__": main()
