#!/usr/bin/env python3
# dmsh_xlate 产物（<key>.spv + <key>.res.json）的 Vulkan 契约校验：模块头/版本、执行模型与入口函数、
# set 0 上的描述符数量与绑定号、描述符种类（cbuf / sbuf / sampled image / storage image）、
# 贴图维度与数组/多重采样位、计算 LocalSize（目标函数、操作数个数、全正维度），必须与反射逐项一致。
# 这是**独立的、需显式调用的预检**：dmsh_xlate、dmsh_build.py 与 Android 消费方目前都不调用它，所以它自己
# 并不能保证任何模块在进管线前被拦下。
# Contract check for dmsh_xlate output (<key>.spv + <key>.res.json): module header/version, execution model and entry
# function, descriptor count/bindings on set 0, descriptor kind (cbuf / sbuf / sampled image / storage image), image
# dimension and arrayed/multisample bits, and the compute LocalSize (target function, operand count, all-positive
# dimensions) must agree with the reflection entry by entry.
# This is a **standalone opt-in preflight**: neither dmsh_xlate, dmsh_build.py nor the Android consumer calls it, so by
# itself it does not guarantee that any module is stopped before a pipeline.
#
# 边界 | Scope:
#   · 只读 dmsh_xlate 自己写的 res.json 与它产出的 SPIR-V；不翻译、不改写、不做完整 SPIR-V 验证（无类型/控制流检查）。
#   · 模块头部的 ID bound 只检查「非 0」，不逐条核对每个结果 id 落在 bound 内——这不是完整的 ID 界校验。
#   · 不比对图像 Format 枚举（Ryujinx 的 TextureFormat → SPIR-V ImageFormat 表未在本仓证明，留作后续）。
#   · 这里是翻译器输出与 Vulkan 消费方之间的接口契约，不是 Maxwell/NVN 语义正确性的证明，也不是已接线的管线闸门。
#   · Reads only the res.json written by dmsh_xlate and the SPIR-V it produced; it does not translate, rewrite, or fully
#     validate SPIR-V (no type/control-flow checks).
#   · The header's ID bound is only checked to be non-zero; individual result ids are not verified against it, so this is
#     not a full ID-bound validation.
#   · Image Format enums are not compared (Ryujinx's TextureFormat -> SPIR-V ImageFormat table is not established in this
#     repository; left as a follow-up).
#   · This is the interface contract between translator output and the Vulkan consumer, not proof of Maxwell/NVN semantics
#     and not a wired pipeline gate.
#
# 用法 | Usage:
#   dmsh_spirv_contract.py <key>.spv <key>.res.json [--local x,y,z] [--quiet]
# 退出码 | Exit codes: 0 = contract holds; 1 = violation (message on stderr); 2 = usage/IO problem
import json, os, struct, sys

MAGIC = 0x07230203
OP_ENTRY_POINT, OP_EXECUTION_MODE, OP_TYPE_IMAGE, OP_TYPE_SAMPLED_IMAGE = 15, 16, 25, 27
OP_TYPE_STRUCT, OP_TYPE_POINTER, OP_DECORATE, OP_VARIABLE = 30, 32, 71, 59
DEC_BINDING, DEC_DESCRIPTOR_SET = 33, 34
MODE_LOCAL_SIZE = 17
SC_UNIFORM, SC_UNIFORM_CONSTANT, SC_STORAGE_BUFFER = 2, 0, 12
STAGE_MODEL = {"vs": 0, "ps": 4, "cs": 5}              # Vertex / Fragment / GLCompute
STAGE_NAME = {v: k for k, v in STAGE_MODEL.items()}
DIM = {0: "1d", 1: "2d", 2: "3d", 3: "cube", 5: "buffer"}
SAMPLER_DIM = {1: "1d", 2: "buffer", 3: "2d", 4: "3d", 5: "cube"}   # Ryujinx SamplerType（SamplerType.cs）
ARRAY_BIT, MS_BIT, SHADOW_BIT = 1 << 8, 1 << 9, 1 << 10
TARGET_ABI = "vulkan1.1"                                # 本校验针对的消费方 ABI | consumer ABI this check targets


class Violation(Exception):
    pass


def need(cond, msg):
    if not cond:
        raise Violation(msg)


def parse(path):
    data = open(path, "rb").read()
    need(len(data) >= 20 and len(data) % 4 == 0, "模块长度 %d 不是 5 字头的 4 字节倍数" % len(data))
    words = struct.unpack("<%dI" % (len(data) // 4), data)
    need(words[0] == MAGIC, "魔数 %#x 不是 SPIR-V" % words[0])
    major, minor = words[1] >> 16, (words[1] >> 8) & 0xff
    need(major == 1 and minor <= 3, "SPIR-V 版本 %d.%d 超出 %s 接受范围（1.0–1.3）" % (major, minor, TARGET_ABI))
    need(words[4] == 0, "schema 应为 0，实为 %d" % words[4])
    need(words[3] >= 1, "头部 ID bound 为 0（本工具只要求非 0，不逐条核对每个结果 id 小于 bound）")
    insts, i = [], 5
    while i < len(words):
        op, wc = words[i] & 0xffff, words[i] >> 16
        need(wc >= 1 and i + wc <= len(words), "指令流在字 %d 处越界（opcode %d，字数 %d）" % (i, op, wc))
        insts.append((op, words[i + 1:i + wc]))
        i += wc
    return insts, words[3], (major, minor)


def string_at(operands, from_index):
    out = b""
    for w in operands[from_index:]:
        chunk = struct.pack("<I", w)
        out += chunk
        if 0 in chunk:
            return out.split(b"\0")[0].decode("utf-8", "replace")
    raise Violation("入口点名字缺少 NUL 结尾")


def analyse(insts):
    entry, modes, types, sampled_images, pointers, decorations, variables = [], [], {}, {}, {}, {}, []
    for op, o in insts:
        if op == OP_ENTRY_POINT:
            need(len(o) >= 3, "OpEntryPoint 操作数不足（缺执行模型/入口函数/名字）")
            entry.append((o[0], o[1], string_at(o, 2)))          # 模型、入口函数 id、名字 | model, entry function id, name
        elif op == OP_EXECUTION_MODE:
            need(len(o) >= 2, "OpExecutionMode 操作数不足")
            modes.append((o[0], o[1], tuple(o[2:])))             # 目标、模式、字面量 | target, mode, literals
        elif op == OP_TYPE_IMAGE:
            types[o[0]] = ("image", {"sampled_type": o[1], "dim": o[2], "depth": o[3], "arrayed": o[4],
                                     "ms": o[5], "sampled": o[6] if len(o) > 6 else 0})
        elif op == OP_TYPE_SAMPLED_IMAGE:
            sampled_images[o[0]] = o[1]
        elif op == OP_TYPE_STRUCT:
            types[o[0]] = ("struct", {})
        elif op == OP_TYPE_POINTER:
            pointers[o[0]] = {"storage": o[1], "pointee": o[2]}
        elif op == OP_DECORATE:
            decorations.setdefault(o[0], {})[o[1]] = tuple(o[2:])
        elif op == OP_VARIABLE:
            variables.append({"id": o[1], "type": o[0], "storage": o[2]})
    return entry, modes, types, sampled_images, pointers, decorations, variables


def kind_of(var, types, sampled_images, pointers):
    ptr = pointers.get(var["type"])
    need(ptr is not None, "变量 %%%d 的指针类型 %%%d 没有定义" % (var["id"], var["type"]))
    need(ptr["storage"] == var["storage"], "变量 %%%d 的存储类与指针类型不一致" % var["id"])
    pointee = ptr["pointee"]
    if var["storage"] == SC_UNIFORM:
        need(types.get(pointee, ("",))[0] == "struct", "Uniform 变量 %%%d 指向的不是结构体（不支持的描述符形态）" % var["id"])
        return "cbuf", None
    if var["storage"] == SC_STORAGE_BUFFER:
        need(types.get(pointee, ("",))[0] == "struct", "StorageBuffer 变量 %%%d 指向的不是结构体" % var["id"])
        return "sbuf", None
    if var["storage"] == SC_UNIFORM_CONSTANT:
        if pointee in sampled_images:
            img = types.get(sampled_images[pointee])
            need(img is not None and img[0] == "image", "SampledImage %%%d 指向的类型不是图像" % pointee)
            return "texture", img[1]
        if types.get(pointee, ("",))[0] == "image":
            return "image", types[pointee][1]
    raise Violation("变量 %%%d（存储类 %d）不是本契约支持的描述符种类" % (var["id"], var["storage"]))


def check_image(binding, kind, info, item):
    need(info["sampled"] == (1 if kind == "texture" else 2),
         "绑定 %d（%s）的 Sampled 标志为 %d，应与种类相符（%s）" % (binding, kind, info["sampled"],
                                                          "1=sampled image" if kind == "texture" else "2=storage image"))
    need(info["dim"] in DIM, "绑定 %d 的图像维度 %d 不是 SPIR-V Dim 枚举值" % (binding, info["dim"]))
    stype = int(item.get("type", 0))
    want_dim = SAMPLER_DIM.get(stype & 0xff)
    need(want_dim is not None, "反射里绑定 %d 的 type=%d 不是 SamplerType 枚举" % (binding, stype))
    got_dim = DIM[info["dim"]]
    need(got_dim == want_dim, "绑定 %d 的维度不一致：模块 %s / 反射 %s（type=%d）" % (binding, got_dim, want_dim, stype))
    need(bool(info["arrayed"]) == bool(stype & ARRAY_BIT),
         "绑定 %d 的数组位不一致：模块 arrayed=%d / 反射 Array=%d" % (binding, info["arrayed"], bool(stype & ARRAY_BIT)))
    need(bool(info["ms"]) == bool(stype & MS_BIT),
         "绑定 %d 的多重采样位不一致：模块 ms=%d / 反射 Multisample=%d" % (binding, info["ms"], bool(stype & MS_BIT)))
    if kind == "texture" and stype & SHADOW_BIT:
        need(info["depth"] != 0, "绑定 %d 在反射里是 Shadow，但模块图像的 Depth 操作数为 0" % binding)


def parse_local(argv):
    """--local x,y,z：缺失取值、个数不对、非数字或非正数都返回 (None, 用法错误)，不抛异常。"""
    """--local x,y,z: a missing value, wrong count, non-numeric or non-positive entry gives (None, usage error), never raises."""
    if "--local" not in argv:
        return None, None
    i = argv.index("--local")
    if i + 1 >= len(argv):
        return None, "--local 缺少取值，应为三个正整数 x,y,z"
    parts = argv[i + 1].split(",")
    if len(parts) != 3 or not all(p.isdigit() for p in parts):
        return None, "--local 的取值必须是三个正整数 x,y,z，实为 %r" % (argv[i + 1],)
    value = tuple(int(p) for p in parts)
    if any(v <= 0 for v in value):
        return None, "--local 的三个维度必须都为正整数，实为 %s" % (value,)
    return value, None


def check_pair(spv, resp, want_local=None):
    """校验一对产物（翻译器 <key>.spv + <key>.res.json），违反契约时抛 Violation。返回 (stage, 版本, 绑定数, LocalSize)。
    Validate one artifact pair (<key>.spv + <key>.res.json from the translator); raise Violation on any contract breach.
    Returns (stage, version, binding count, LocalSize)."""
    try:
        insts, bound, version = parse(spv)
        entry, modes, types, sampled_images, pointers, decorations, variables = analyse(insts)
        try:
            res = json.load(open(resp, encoding="utf-8"))
        except (OSError, ValueError) as e:
            raise Violation("反射 JSON 读不了：%s" % e)
        stage = res.get("stage")
        need(stage in STAGE_MODEL, "反射 stage=%r 不是 vs/ps/cs（不支持该阶段，拒绝而不是猜）" % stage)
        need(len(entry) == 1, "模块有 %d 个 OpEntryPoint，契约要求 1 个" % len(entry))
        model, entry_id, name = entry[0]
        need(model == STAGE_MODEL[stage], "执行模型 %d（%s）与反射 stage=%s 不符"
             % (model, "vertex" if model == 0 else "fragment" if model == 4 else "glcompute" if model == 5 else "其他", stage))
        need(name == "main", "入口点名字是 %r，契约要求 \"main\"" % name)
        local_modes = [(target, dims) for (target, mode, dims) in modes if mode == MODE_LOCAL_SIZE]
        if stage == "cs":
            need(local_modes, "计算着色器缺 OpExecutionMode LocalSize")
            need(len(local_modes) == 1, "LocalSize 声明了 %d 次（重复或冲突），契约要求恰好一次" % len(local_modes))
            target, dims = local_modes[0]
            need(target == entry_id, "LocalSize 的目标函数 %d 不是入口函数 %d" % (target, entry_id))
            need(len(dims) == 3, "LocalSize 只带 %d 个维度，契约要求 3 个" % len(dims))
            need(all(d > 0 for d in dims), "LocalSize 的三个维度必须全为正数，实为 %s" % (dims,))
            if want_local is not None:
                need(dims == want_local, "LocalSize %s 与作业声明的 %s 不一致" % (dims, want_local))
        else:
            need(not local_modes, "非计算着色器却带 LocalSize 执行模式")

        # 反射四类绑定：数量与种类必须与模块一一对应。 | The four reflection categories must match the module one to one.
        declared = {}
        for cat, kind in (("cbufs", "cbuf"), ("sbufs", "sbuf"), ("textures", "texture"), ("images", "image")):
            for item in res.get(cat, []):
                b = int(item["binding"])
                need(b not in declared, "反射里绑定 %d 重复出现（%s 与 %s）" % (b, declared[b][0] if b in declared else "?", cat))
                declared[b] = (cat, kind, item)
        found = {}
        for var in variables:
            if var["storage"] not in (SC_UNIFORM, SC_UNIFORM_CONSTANT, SC_STORAGE_BUFFER):
                continue   # 私有/工作组等变量不是描述符 | private/workgroup variables are not descriptors
            kind, info = kind_of(var, types, sampled_images, pointers)
            dec = decorations.get(var["id"], {})
            need(DEC_BINDING in dec, "描述符变量 %%%d 没有 Binding 装饰" % var["id"])
            need(DEC_DESCRIPTOR_SET in dec, "描述符变量 %%%d 没有 DescriptorSet 装饰" % var["id"])
            need(dec[DEC_DESCRIPTOR_SET][0] == 0, "描述符变量 %%%d 在 set %d，契约要求 set 0" % (var["id"], dec[DEC_DESCRIPTOR_SET][0]))
            b = dec[DEC_BINDING][0]
            need(b not in found, "模块里绑定 %d 重复出现" % b)
            found[b] = (kind, info)
        missing = sorted(set(declared) - set(found))
        extra = sorted(set(found) - set(declared))
        need(not missing, "反射声明了模块里没有的绑定：%s" % missing)
        need(not extra, "模块里有反射没声明的绑定：%s" % extra)
        for b in sorted(declared):
            cat, kind, item = declared[b]
            got_kind, info = found[b]
            need(got_kind == kind, "绑定 %d 的种类不一致：模块 %s / 反射 %s" % (b, got_kind, cat))
            if kind in ("texture", "image"):
                check_image(b, kind, info, item)
        local_dims = local_modes[0][1] if stage == "cs" else None
        return stage, tuple(version), len(found), local_dims
    except Violation:
        raise
    except OSError as e:
        raise Violation("无法读取输入：%s" % e)


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__ or "usage: dmsh_spirv_contract.py <spv> <res.json> [--local x,y,z]\n")
        return 2
    spv, resp = argv[1], argv[2]
    quiet = "--quiet" in argv
    want_local, usage = parse_local(argv)
    if usage:
        sys.stderr.write("用法错误：%s\n" % usage)
        return 2
    try:
        stage, version, nbind, local_dims = check_pair(spv, resp, want_local)
    except Violation as e:
        sys.stderr.write("FAIL %s: %s\n" % (os.path.basename(spv), e))
        return 1
    except Exception as e:   # 契约外的输入形态也不抛栈：报内部错误并失败 | unexpected input shapes also fail without a traceback
        sys.stderr.write("FAIL %s: 内部错误 %s: %s\n" % (os.path.basename(spv), type(e).__name__, e))
        return 1
    if not quiet:
        res = json.load(open(resp, encoding="utf-8"))
        print("OK %s: stage=%s spirv=%d.%d bindings=%d (cbuf %d, sbuf %d, tex %d, img %d)%s"
              % (res.get("key", "?"), stage, version[0], version[1], nbind,
                 len(res.get("cbufs", [])), len(res.get("sbufs", [])),
                 len(res.get("textures", [])), len(res.get("images", [])),
                 "" if local_dims is None else " local=%dx%dx%d" % local_dims))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
