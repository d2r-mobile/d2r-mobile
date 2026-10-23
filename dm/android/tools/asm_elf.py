# 苹果（Mach-O）汇编 → 安卓（ELF）汇编：宿主的 .S 只写一份苹果写法（含生成的 nvn_thunks.S），安卓构建时过一遍本脚本再汇编。
# 用法：asm_elf.py <输入.S> <输出.S>
# 只做语法层的一一对应，不改任何指令：
#   C 符号的下划线前缀：_name → name（Mach-O 给 C 符号加 _，ELF 不加）
#   adrp / add 的页寻址：sym@PAGE → sym，sym@PAGEOFF → :lo12:sym
#   局部标号：本文件定义的 Lxxx → .Lxxx（ELF 的局部标号前缀）
#   .cstring → 只读字符串段；.private_extern → .hidden；文件末尾补不可执行栈声明
# Apple (Mach-O) assembly → Android (ELF) assembly: the host's .S files are written once in Apple syntax (including the generated nvn_thunks.S), and the Android build runs them through this script before assembling.
# Usage: asm_elf.py <input.S> <output.S>
# Only one-to-one syntax mapping, no instruction is changed:
#   underscore prefix of C symbols: _name → name (Mach-O prefixes C symbols with _, ELF does not)
#   page addressing for adrp / add: sym@PAGE → sym, sym@PAGEOFF → :lo12:sym
#   local labels: Lxxx defined in this file → .Lxxx (the ELF local label prefix)
#   .cstring → read-only string section; .private_extern → .hidden; a non-executable stack note is appended at the end of the file
import re, sys

src, dst = sys.argv[1], sys.argv[2]
text = open(src, encoding='utf-8').read()
locals_ = set(re.findall(r'^\s*(L\w+):', text, re.M))
out = []
for line in text.split('\n'):
    code, sep, comment = line.partition('//')
    code = re.sub(r'(\w+)@PAGEOFF\b', r':lo12:\1', code)
    code = re.sub(r'(\w+)@PAGE\b', r'\1', code)
    code = re.sub(r'(?<![\w.$])_(\w+)', r'\1', code)
    if locals_: code = re.sub(r'(?<![\w.])(L\w+)\b', lambda m: '.' + m.group(1) if m.group(1) in locals_ else m.group(1), code)
    code = re.sub(r'^(\s*)\.cstring\b', r'\1.section .rodata.str1.1,"aMS",@progbits,1', code)
    code = re.sub(r'^(\s*)\.private_extern\b', r'\1.hidden', code)
    out.append(code + sep + comment)
out.append('    .section .note.GNU-stack,"",@progbits')
open(dst, 'w', encoding='utf-8', newline='\n').write('\n'.join(out) + '\n')
