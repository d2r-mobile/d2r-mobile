#!/usr/bin/env python3
# 做 Vulkan 后端要的 4 个查询核着色器包：用 glslc 把 dm/gfx/vulkan/kernels/builtin_queries.comp 按 MODE 0–3 编成 SPIR-V，
# 再用 pack_game_shaders.py 打成 <键>.xgs。键与 MSL 版同取 dm/gfx/nvn/nvn_builtin.h 的表（main 程序本体内嵌的 4 个计算着色器）。
# 这 4 个核是手写的，不来自游戏文件，所以能从源码重做；另外 6,927 个包是游戏着色器的翻译，不在这里做。
# 绑定与 MSL 版一致：0 = uniform 缓冲 0（元素个数，16 字节），1 = 存储缓冲 0（计数器报告），2 = 存储缓冲 1（结果）。
# Builds the 4 query-kernel shader bundles the Vulkan backend needs: glslc compiles dm/gfx/vulkan/kernels/builtin_queries.comp
# into SPIR-V for MODE 0-3, and pack_game_shaders.py packs each into <key>.xgs. The keys come from the table in
# dm/gfx/nvn/nvn_builtin.h, as for the MSL versions (the 4 compute shaders embedded in the main program itself).
# These kernels are hand-written, not taken from the game files, so they can be rebuilt from source; the other 6,927 bundles
# are translations of the game's shaders and are not made here.
# Bindings as in the MSL versions: 0 = uniform buffer 0 (element count, 16 bytes), 1 = storage buffer 0 (counter reports),
# 2 = storage buffer 1 (results).
#
# 用法 | Usage: pack_builtin_queries.py --output <dm/ 以外的新目录 | new directory outside dm/> [--glslc <path>]
# glslc 缺省依次找 $GLSLC、PATH、NDK 的 shader-tools（$ANDROID_NDK_HOME、$ANDROID_HOME/ndk/*）。
# glslc defaults to $GLSLC, then PATH, then the NDK's shader-tools ($ANDROID_NDK_HOME, $ANDROID_HOME/ndk/*).
import argparse, glob, hashlib, json, os, pathlib, re, shutil, subprocess, tempfile

from pack_game_shaders import pack

DM = pathlib.Path(__file__).resolve().parents[2]
SOURCE = DM / 'gfx/vulkan/kernels/builtin_queries.comp'
# builtin_queries.comp 的 MODE 0–3。
# MODE 0-3 of builtin_queries.comp.
MODES = ('q_val0', 'q_val8', 'q_dlt0', 'q_dlt8')
DESCRIPTORS = [
    {'name': 'count', 'binding': 0, 'type': 'uniform_buffer', 'count': 1, 'block': {'bytes': 16},
     'source': {'category': 'nvn', 'nvn': {'kind': 'uniform', 'slot': 0}}},
    {'name': 'src', 'binding': 1, 'type': 'storage_buffer', 'count': 1,
     'source': {'category': 'nvn', 'nvn': {'kind': 'storage_pointer', 'slot': 0}}},
    {'name': 'dst', 'binding': 2, 'type': 'storage_buffer', 'count': 1,
     'source': {'category': 'nvn', 'nvn': {'kind': 'storage_pointer', 'slot': 1}}},
]


def find_glslc(given):
    found = given or os.environ.get('GLSLC') or shutil.which('glslc')
    if found:
        return found
    roots = [os.environ.get(v) for v in ('ANDROID_NDK_HOME', 'ANDROID_NDK_ROOT')]
    for sdk in (os.environ.get('ANDROID_HOME'), os.environ.get('ANDROID_SDK_ROOT')):
        if sdk:
            roots += sorted(glob.glob(os.path.join(sdk, 'ndk', '*')), reverse=True)
    for root in filter(None, roots):
        hits = glob.glob(os.path.join(root, 'shader-tools', '*', 'glslc*'))
        if hits:
            return hits[0]
    raise SystemExit('glslc not found: pass --glslc, set GLSLC, or install the Android NDK')


def kernel_keys():
    text = (DM / 'gfx/nvn/nvn_builtin.h').read_text(encoding='utf-8')
    keys = {name: key for key, name in re.findall(r'\{"([0-9a-f]{16})", "(q_\w+)"\}', text)}
    missing = [name for name in MODES if name not in keys]
    if missing:
        raise SystemExit(f'nvn_builtin.h has no key for {missing}')
    return keys


def main():
    ap = argparse.ArgumentParser(description='build the 4 query-kernel .xgs bundles from builtin_queries.comp')
    ap.add_argument('--output', type=pathlib.Path, required=True)
    ap.add_argument('--glslc')
    args = ap.parse_args()
    if args.output.resolve().is_relative_to(DM):
        raise SystemExit('output must be outside dm/')
    glslc, keys = find_glslc(args.glslc), kernel_keys()
    args.output.mkdir(parents=True, exist_ok=False)
    with tempfile.TemporaryDirectory() as work:
        for mode, name in enumerate(MODES):
            key = keys[name]
            spv = pathlib.Path(work, key + '.spv')
            subprocess.run([glslc, '-fshader-stage=comp', f'-DMODE={mode}', str(SOURCE), '-o', str(spv)], check=True)
            reflection = {'identity': {'key': key, 'name': name}, 'stage': 'cs', 'descriptors': DESCRIPTORS,
                          'frozen_job': {}, 'sha256': hashlib.sha256(spv.read_bytes()).hexdigest()}
            meta = pathlib.Path(work, key + '.vk.json')
            meta.write_text(json.dumps(reflection, indent=2) + '\n', encoding='utf-8')
            key, data = pack(meta)
            (args.output / (key + '.xgs')).write_bytes(data)
            print(f'{key}.xgs {name} {len(data)} bytes sha256 {hashlib.sha256(data).hexdigest()}')


if __name__ == '__main__':
    main()
