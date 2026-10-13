#!/usr/bin/env python3
# shell 脚本里「$变量」后面紧跟非 ASCII 字符（中文标点、汉字）的写法一律要写成 ${名}。
# macOS 自带的 bash 3.2 在 UTF-8 区域设置下（终端的缺省状态）会把后面那个多字节字符的首字节并进变量名：
# "（$X）" 被读成变量 X\xEF，set -u 下整个脚本中止，没有 set -u 时静默展开成空。我们的后台任务不设区域变量，所以自己跑不出来。
# 用法：brace_scan.py <目录树>   列出每处「文件:行号:片段」；代码里有命中时退出码 1。整行注释与行尾注释里的不算。
# In shell scripts, "$variable" directly followed by a non-ASCII character (Chinese punctuation, Chinese characters) must always be written as ${name}.
# The bash 3.2 shipped with macOS, in a UTF-8 locale (the default state of a terminal), merges the first byte of the following multibyte character into the variable name:
# "（$X）" is read as the variable X\\xEF, which aborts the whole script under set -u and silently expands to nothing without set -u. Our background jobs set no locale variables, so we never hit it ourselves.
# Usage: brace_scan.py <tree>   lists each hit as "file:line:snippet"; exits 1 when there are hits in code. Hits in whole-line comments and trailing comments do not count.
import os, re, sys
T = sys.argv[1]
RX = re.compile(rb'\$([A-Za-z_][A-Za-z0-9_]*)(?=[\x80-\xff])')
def is_shell(p):
    if p.endswith('.sh'): return True
    if '.' in os.path.basename(p): return False
    try:
        with open(p, 'rb') as f: h = f.readline()
        return h.startswith(b'#!') and b'sh' in h
    except OSError: return False
def code_part(l):
    """去掉行尾注释：不在引号里、且前面是行首或空白的 # 之后都算注释。 | Strip the trailing comment: everything after a # that is not inside quotes and is preceded by the start of the line or whitespace counts as a comment."""
    q = None; i = 0
    while i < len(l):
        c = l[i:i+1]
        if q:
            if c == b'\\' and q == b'"': i += 2; continue
            if c == q: q = None
        elif c in (b'"', b"'"): q = c
        elif c == b'\\': i += 2; continue
        elif c == b'#' and (i == 0 or l[i-1:i] in (b' ', b'\t', b';')): return l[:i]
        i += 1
    return l
n = 0
for d, ds, fs in os.walk(T):
    ds[:] = [x for x in ds if x not in ('.git', 'third_party', 'Gen', 'build') and not x.startswith('.iosdd')]
    for f in sorted(fs):
        p = os.path.join(d, f)
        if os.path.islink(p) or not is_shell(p): continue
        with open(p, 'rb') as fh: L = fh.read().split(b'\n')
        for i, l in enumerate(L):
            c = code_part(l)
            for m in RX.finditer(c):
                print('%s:%d:%s' % (os.path.relpath(p, T), i + 1, c[max(0, m.start() - 20):m.end() + 9].decode('utf-8', 'replace'))); n += 1
print('BRACE %s：变量后紧跟非 ASCII 字符而没写花括号 %d 处' % ('OK' if n == 0 else 'RED', n))
sys.exit(1 if n else 0)
