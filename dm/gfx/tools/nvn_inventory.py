#!/usr/bin/env python3
# 清点 NVN 入口的「必需 / 源码有定义 / 记名空实现 / 报红桩」四态，并给出调用点计数与优先级。
# 「源码有定义」只表示 hle_nvn.c 里有 NVN(...) 条目，语义正确性与完备性未经验证（见 docs/android/nvn-inventory.md）。
# 全部输入都是仓内已有文件：names.txt（游戏取址的 523 个名字）、nvn_slots.tsv（静态调用面，nvn_slots.py 出）、
# hle_nvn.c（实现与 g_nops 空实现表）、nvn_obj.h（录制操作枚举，用来核对手写操作名单）。
# 本工具不读游戏文件、不写任何产物到源码树之外。
# Inventory of NVN entries in four states (required / source-defined / documented no-op / red stub), with call-site counts
# and priority. "source-defined" only means an NVN(...) entry exists in hle_nvn.c; semantic correctness and completeness
# are unverified (see docs/android/nvn-inventory.md).
# All inputs already live in the repository: names.txt (the 523 names the game looks up), nvn_slots.tsv (static call
# surface, produced by nvn_slots.py), hle_nvn.c (implementations and the g_nops no-op list) and nvn_obj.h (the record
# operation enum, used to check the hand-written operation list).
# The tool reads no game files and writes nothing outside where it is told to.
#
# 用法 | Usage:
#   nvn_inventory.py                    列出汇总与 TSV（stdout）
#   nvn_inventory.py --tsv OUT          汇总进 stderr，逐条清单写 OUT
#   nvn_inventory.py --check            只做一致性检查：必需项没有实现/空实现即失败（退出码 1）
#   nvn_inventory.py --md OUT           逐条清单写成 Markdown 表
#
# 退出码 | Exit codes: 0 = 检查通过 / 仅输出成功；1 = 检查失败
import argparse, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
NVN = os.path.normpath(os.path.join(HERE, "..", "nvn"))

# 命令缓冲录制操作（nvn_obj.h 的 op 枚举）；文档里的「命令清单」直接由这里同步
# Command-buffer record operations (the op enum in nvn_obj.h); the command list in the docs stays in sync with this
OPS = """OP_END OP_JUMP OP_BEGIN OP_CALL OP_SIGNAL_EVENT OP_REPORT_COUNTER OP_COPY_B2B OP_UPDATE_UB
OP_SET_RT OP_CLEAR_COLOR OP_CLEAR_DS OP_VIEWPORTS OP_SCISSOR OP_DEPTH_RANGE OP_BIND_PROGRAM
OP_BIND_STATE OP_BIND_VATTR OP_BIND_VSTREAM OP_BIND_VBUF OP_BIND_UBO OP_BIND_SSBO OP_BIND_TEX
OP_BIND_SAMP OP_STENCIL OP_POLY_OFFSET OP_DRAW OP_DRAW_IDX OP_DISPATCH OP_BARRIER OP_SET_TEX_POOL
OP_SET_SAMP_POOL OP_COPY_B2T OP_COPY_STRIDE OP_BLEND_COLOR OP_COPY_T2B OP_BIND_IMG OP_COPY_T2T
OP_RESET_COUNTER OP_CLEAR_TEX OP_COPY_IMAGE_STRIDE""".split()

# 入口按名字第二段归类（nvnCommandBufferDrawArrays → CommandBuffer）；这些类别是文档里的对象／命令分组依据
# Entries are grouped by the second name segment (nvnCommandBufferDrawArrays -> CommandBuffer); these categories are
# what the object/command groups in the docs are built from
GROUP_ORDER = ["Device", "MemoryPool", "TexturePool", "SamplerPool", "Texture", "Sampler", "Program",
               "Sync", "Event", "Queue", "Window", "CommandBuffer"]


def read(path):
    # 输入缺失或读不了时报清楚是哪一份，不抛栈回溯
    # A missing or unreadable input names the file instead of raising a traceback
    try:
        with open(path, encoding="utf-8") as f:
            return f.read()
    except OSError as e:
        raise SystemExit("输入不可读 | input not readable: %s (%s)" % (path, e.strerror))


def load_ops():
    """nvn_obj.h 的 op 枚举 → 名字表；工具的 OPS 是人工转写，这里用来核对，不一致即失败
    The op enum in nvn_obj.h -> names; the tool's OPS list is a manual transcription and is checked against it, mismatch fails"""
    src = read(os.path.join(NVN, "nvn_obj.h"))
    names, inside = [], False
    for line in src.splitlines():
        if re.search(r"enum\s*\{", line) and not inside:
            inside = True
        if not inside:
            continue
        code = line.split("//")[0]
        if re.search(r"\}\s*op_enum|OP_COUNT_", code):
            for m in re.finditer(r"\bOP_[A-Z0-9_]+\b", code):
                if m.group(0) != "OP_COUNT_":
                    names.append(m.group(0))
            break
        for m in re.finditer(r"\bOP_[A-Z0-9_]+\b", code):
            names.append(m.group(0))
    return names


def load_slots():
    """名字 → (调用点, 首个调用点)；槽地址不进清单（与优先级无关） | name -> (call sites, first call site)"""
    out, order, bad = {}, [], []
    for no, line in enumerate(read(os.path.join(NVN, "nvn_slots.tsv")).splitlines(), 1):
        if not line or line.startswith("#"):
            continue
        f = line.split("\t")
        if f[0] == "name":
            continue
        if len(f) < 3:
            bad.append("nvn_slots.tsv 第 %d 行列数不足（%d） | line %d has %d columns" % (no, len(f), no, len(f)))
            continue
        if not f[2].isdigit():
            bad.append("nvn_slots.tsv 第 %d 行调用点数不是数字：%r | line %d call count is not a number: %r" % (no, f[2], no, f[2]))
            continue
        if f[0] in out:
            bad.append("nvn_slots.tsv 第 %d 行名字重复：%s | duplicate name at line %d: %s" % (no, f[0], no, f[0]))
            continue
        out[f[0]] = (int(f[2]), f[3] if len(f) > 3 and f[3] != "-" else "")
        order.append(f[0])
    return out, order, bad


def load_names():
    names = [l.strip() for l in read(os.path.join(NVN, "names.txt")).splitlines() if l.strip()]
    return names


def load_impls():
    """从 hle_nvn.c 取实现名与行号，以及 g_nops 空实现名单 | implementations + line numbers, and the g_nops names"""
    src = read(os.path.join(NVN, "hle_nvn.c")).splitlines()
    impl, noproto = {}, set()
    for i, line in enumerate(src, 1):
        m = re.match(r"NVN\((\w+),", line)
        if m:
            impl.setdefault(m.group(1), i)
            continue
        m = re.match(r"NVN_NOPROTO\((\w+),", line)
        if m:
            impl.setdefault(m.group(1), i)
            noproto.add(m.group(1))
    # g_nops：块内以引号开头的名字；注释行以 // 开头，先剥掉
    nops, inside = [], False
    for line in src:
        if "g_nops[] = {" in line:
            inside = True
            continue
        if not inside:
            continue
        if line.strip().startswith("};"):
            break
        code = line.split("//")[0]
        for m in re.finditer(r'"(nvn\w+)"', code):
            nops.append(m.group(1))
    return impl, noproto, nops


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tsv")
    ap.add_argument("--md")
    ap.add_argument("--check", action="store_true")
    a = ap.parse_args()

    names = load_names()
    slots, order, tsv_bad = load_slots()
    impl, noproto, nops = load_impls()

    errs = list(tsv_bad)
    seen = set()
    for n in names:
        if n in seen:
            errs.append("名字表里重复：%s | duplicate name in names.txt: %s" % (n, n))
        seen.add(n)
    for n in names:
        if n not in slots:
            errs.append("名字表里有、调用面表里没有：%s | in names.txt but not in nvn_slots.tsv: %s" % (n, n))
    for n in slots:
        if n not in names:
            errs.append("调用面表里有、名字表里没有：%s | in nvn_slots.tsv but not in names.txt: %s" % (n, n))
    for n in impl:
        if n not in names:
            errs.append("实现了名字表以外的接口：%s | implemented but not in names.txt: %s" % (n, n))
    for n in nops:
        if n not in names:
            errs.append("空实现名字不在名字表里：%s | no-op name not in names.txt: %s" % (n, n))
        if n in impl:
            errs.append("既列为空实现又有实现：%s | both implemented and listed as a no-op: %s" % (n, n))

    rows = []
    for n in order:
        calls, first = slots.get(n, (0, ""))
        if n in nops:
            kind = "nop"
        elif n in impl:
            kind = "impl"
        else:
            kind = "stub"
        grp = "Other"
        m = re.match(r"nvn(\w+?)(?:[A-Z]|$)", n[3:])
        rest = n[3:]
        for g in GROUP_ORDER:
            if rest.startswith(g):
                grp = g
                break
        rows.append({"name": n, "calls": calls, "first": first, "kind": kind, "line": impl.get(n, 0),
                     "group": grp, "noproto": n in noproto})

    req = [r for r in rows if r["calls"] > 0]
    miss = [r for r in req if r["kind"] == "stub"]
    for r in miss:
        errs.append("有调用点但只接报红桩：%s（%d 个调用点） | has call sites but only a red stub: %s" % (r["name"], r["calls"], r["name"]))

    hdr_ops = load_ops()
    if not hdr_ops:
        errs.append("nvn_obj.h 里没解析到 op 枚举 | no op enum parsed from nvn_obj.h")
    if len(hdr_ops) != len(set(hdr_ops)):
        errs.append("nvn_obj.h 的 op 枚举有重名 | duplicate op names in the nvn_obj.h enum")
    if set(hdr_ops) != set(OPS):
        miss_op = sorted(set(hdr_ops) - set(OPS))
        extra_op = sorted(set(OPS) - set(hdr_ops))
        errs.append("本工具人工转写的 op 名单与 nvn_obj.h 不一致：缺 %s，多 %s | the manual op list disagrees with nvn_obj.h: missing %s, extra %s"
                    % (miss_op, extra_op, miss_op, extra_op))

    counts = {
        "names": len(names),
        "required": len(req),
        "impl": sum(1 for r in rows if r["kind"] == "impl"),
        "nop": sum(1 for r in rows if r["kind"] == "nop"),
        "stub": sum(1 for r in rows if r["kind"] == "stub"),
        "required_impl": sum(1 for r in req if r["kind"] == "impl"),
        "required_nop": sum(1 for r in req if r["kind"] == "nop"),
        "required_stub": len(miss),
        "noproto_impl": sum(1 for r in rows if r["noproto"]),
        "ops": len(hdr_ops),
    }

    def table(out):
        out.write("# name\tgroup\tcalls\tfirst_use\tstate\thle_nvn.c line\n")
        for r in rows:
            out.write("%s\t%s\t%d\t%s\t%s\t%s\n" % (r["name"], r["group"], r["calls"], r["first"] or "-", r["kind"],
                                                    r["line"] or "-"))

    def md(out):
        out.write("| 入口 / Entry | 组 / Group | 调用点 / Calls | 状态 / State | hle_nvn.c |\n|---|---|---:|---|---:|\n")
        for r in sorted(rows, key=lambda r: (-r["calls"], r["name"])):
            line = str(r["line"]) if r["line"] else ""
            st = {"impl": "实现 / impl", "nop": "空实现 / no-op", "stub": "报红桩 / red stub"}[r["kind"]]
            out.write("| `%s` | %s | %d | %s | %s |\n" % (r["name"], r["group"], r["calls"], st, line))

    if a.tsv:
        with open(a.tsv, "w", encoding="utf-8") as f:
            table(f)
            f.write("# ops: %s\n" % " ".join(OPS))
    else:
        table(sys.stdout)
    if a.md:
        with open(a.md, "w", encoding="utf-8") as f:
            md(f)

    # 按组汇总（文档里的分对象表用这些数字） | Per-group summary (these numbers feed the object tables in the docs)
    sys.stderr.write("NVN 入口清点 | NVN entry inventory\n")
    sys.stderr.write("  名字 %d，有调用点 %d；实现 %d（其中必需 %d），空实现 %d（必需 %d），报红桩 %d（必需 %d），无原型实现 %d\n" % (
        counts["names"], counts["required"], counts["impl"], counts["required_impl"], counts["nop"],
        counts["required_nop"], counts["stub"], counts["required_stub"], counts["noproto_impl"]))
    groups = {}
    for r in rows:
        g = groups.setdefault(r["group"], {"all": 0, "impl": 0, "nop": 0, "stub": 0, "calls": 0, "req": 0})
        g["all"] += 1
        g[r["kind"]] += 1
        g["calls"] += r["calls"]
        g["req"] += 1 if r["calls"] else 0
    for g in GROUP_ORDER + sorted(set(groups) - set(GROUP_ORDER)):
        if g not in groups:
            continue
        v = groups[g]
        sys.stderr.write("  %-14s 共 %3d 必需 %3d 实现 %3d 空实现 %2d 桩 %3d 调用点 %6d\n" % (
            g, v["all"], v["req"], v["impl"], v["nop"], v["stub"], v["calls"]))
    sys.stderr.write("  录制操作符 %d 个 | %d record operations\n" % (counts["ops"], counts["ops"]))

    if errs:
        sys.stderr.write("\n不合格 | FAILED (%d):\n" % len(errs))
        for e in errs:
            sys.stderr.write("  - " + e + "\n")
        return 1
    if a.check:
        sys.stderr.write("\n一致性检查通过：必需项全部有实现或空实现 | consistency check passed: every required entry is implemented or a documented no-op\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
