#!/usr/bin/env python3
# 从 nvn.h 的 PFNNVN*PROC 原型生成宿主侧签名检查表 nvn_protos.h。
# 苹果 arm64 与 Switch 的 AAPCS64 差异：<32 位整型参数苹果要求调用方扩展、Switch 不扩展 ⇒
#   NVNboolean 参数在宿主签名里一律换成 uint32_t（实现里只看低 8 位）；其余 <32 位参数出现即报红。
# 栈传参（>8 个整型或 >8 个浮点参数）两边打包规则不同 ⇒ 出现即报红。
# 用法：gen_protos.py <nvn.h> <名字表(一行一个, 加载顺序)> <输出 .h>
# Generate the host-side signature check table nvn_protos.h from the PFNNVN*PROC prototypes in nvn.h.
# Difference between Apple arm64 and Switch AAPCS64: for integer arguments narrower than 32 bits, Apple requires the caller to extend them and Switch does not ⇒
#   NVNboolean parameters are always replaced with uint32_t in the host signature (the implementation only looks at the low 8 bits); any other <32-bit parameter is reported as a failure on sight.
# Stack-passed arguments (>8 integer or >8 floating-point parameters) are packed differently on the two sides ⇒ reported a failure on sight.
# Usage: gen_protos.py <nvn.h> <name table (one per line, in load order)> <output .h>
import re, sys

hdr, names_path, out = sys.argv[1:4]
src = open(hdr).read()
protos = {}
for m in re.finditer(r'typedef\s+([\w\s\*]+?)\s*\(\*PFNNVN(\w+)PROC\)\s*\(([^;]*?)\);', src, re.S):
    ret, key, args = m.group(1).strip(), m.group(2), ' '.join(m.group(3).split())
    protos[key] = (ret, [a.strip() for a in args.split(',')] if args.strip() not in ('', 'void') else [])

FP = re.compile(r'^(const\s+)?(float|double)$')
SMALL = re.compile(r'^(const\s+)?(uint8_t|int8_t|uint16_t|int16_t|char|unsigned char|short|unsigned short|bool)$')
names = [l.strip() for l in open(names_path) if l.strip()]
lines = ['// 由 dm/gfx/tools/gen_protos.py 生成，勿手改。NVNboolean 参数已换成 uint32_t。', '#pragma once', '#include "nvn.h"', '']
red, missing = [], []
for n in names:
    key = n[3:].upper()
    if key not in protos:
        missing.append(n)
        continue
    ret, args = protos[key]
    gp = fp = 0
    conv = []
    for a in args:
        t = re.sub(r'\s+\w+$', '', a) if not a.endswith('*') and len(a.split()) > 1 and a.split()[-1] not in ('int', 'float', 'double', 'char') and not a.split()[-1].endswith('_t') and not a.split()[-1].startswith(('NVN', 'PFN')) else a
        t = t.strip()
        if FP.match(t): fp += 1
        else: gp += 1
        if t == 'NVNboolean': t = 'uint32_t'
        elif SMALL.match(t): red.append(f'{n}: <32 位参数 {t}')
        conv.append(t)
    if gp > 8 or fp > 8: red.append(f'{n}: 栈传参 gp={gp} fp={fp}')
    lines.append(f'typedef {ret} (*NVNT_{n})({", ".join(conv) if conv else "void"});')
lines.append('')
lines.append('// 头文件里没有原型的名字（新版接口）：只能接记名桩')
for n in missing: lines.append(f'//   {n}')
open(out, 'w').write('\n'.join(lines) + '\n')
print(f'原型 {len(names) - len(missing)} / 名字 {len(names)}；无原型 {len(missing)}；报红 {len(red)}')
for r in red: print('  红', r)

# 计数跳板：每个名字一个静态汇编入口 _nvnth_<i>，原子累加 nvn_slot[i].cnt 后跳到 nvn_slot[i].fn。
# 不在运行期生成代码（iOS 无 JIT），x16/x17 是过程间暂存寄存器，跳板可自由使用。
# 跳进目标时 x16 仍指向本槽：未实现桩 nvn_unimpl_entry 据此反查名字。
# Counting trampolines: one static assembly entry _nvnth_<i> per name, which atomically increments nvn_slot[i].cnt and then jumps to nvn_slot[i].fn.
# No code is generated at runtime (iOS has no JIT); x16/x17 are intra-procedure-call scratch registers, so the trampoline may use them freely.
# On entry to the target, x16 still points to this slot: the unimplemented stub nvn_unimpl_entry uses it to look the name back up.
th = out.replace('nvn_protos.h', 'nvn_thunks.S')
S = ['// 由 dm/gfx/tools/gen_protos.py 生成，勿手改。',
     '// 计数用 LSE 原子 stadd（ARMv8.1；Apple A12 / M1 起全部支持）：下一行让本文件不依赖编译器的 -mcpu 基线。',
     '    .arch_extension lse', '    .text', '    .p2align 2']
for i, n in enumerate(names):
    S += [f'    .globl _nvnth_{i}', f'_nvnth_{i}:',
          '    adrp x16, _nvn_slot@PAGE', '    add x16, x16, _nvn_slot@PAGEOFF',
          f'    mov x17, #{i * 16}', '    add x16, x16, x17',
          '    mov x17, #1', '    stadd x17, [x16]', '    ldr x17, [x16, #8]', '    br x17']
S += ['    .data', '    .p2align 3', '    .globl _nvn_thunk', '_nvn_thunk:'] + [f'    .quad _nvnth_{i}' for i in range(len(names))]
S += ['    .globl _nvn_names', '_nvn_names:'] + [f'    .quad Ln{i}' for i in range(len(names))]
S += ['    .cstring'] + [f'Ln{i}: .asciz "{n}"' for i, n in enumerate(names)]
open(th, 'w').write('\n'.join(S) + '\n')
hn = out.replace('nvn_protos.h', 'nvn_count.h')
open(hn, 'w').write('// 由 gen_protos.py 生成\n#pragma once\n#define NVN_NFUNC %d\n' % len(names))
