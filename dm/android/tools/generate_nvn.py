#!/usr/bin/env python3
"""转换原 NVN ARM64 计数跳板为 ELF。| Convert the upstream NVN ARM64 counting thunks to ELF."""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
for name in ("nvn_thunks", "nvn_entry"):
    text = (root / "gfx/nvn" / (name + ".S")).read_text()
    text = re.sub(r"\b_(nvn\w*|mach_absolute_time)\b", r"\1", text)
    text = re.sub(r"(\w+)@PAGEOFF", r":lo12:\1", text)
    text = text.replace("@PAGE", "")
    # 直接 ADRP 地址必须绑定到本共享库，不能成为可被替换的外部符号。
    # Direct ADRP addresses must bind to this shared library rather than preemptible external symbols.
    text = ".hidden nvn_slot, nvn_real, nvn_prof_ticks\n" + text
    text = text.replace("bl mach_absolute_time", "bl dm_android_nvn_time")
    # 名字和函数地址表含重定位，放进 RELRO。| Relocatable names and function pointer tables belong in RELRO.
    text = text.replace("    .data", '    .section .data.rel.ro,"aw",%progbits')
    text = text.replace("    .cstring", '    .section .rodata,"a",%progbits')
    text += '\n.section .note.GNU-stack,"",%progbits\n'
    (out / (name + "_elf.S")).write_text(text)
