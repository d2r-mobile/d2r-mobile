#!/usr/bin/env python3
# 脚本里用 pgrep -f / pkill -f 按命令行文本判断「某任务在不在跑」、或按名字收进程的写法一律报红。
# 按文本匹配会命中任何命令行里带这个词的进程：等待任务自己（until ! pgrep -f merge.sh 匹配到自己，白等 33 分钟）、
# 别人的同名任务、编辑器 / grep / ssh 里的字符串；pkill -f 还会把它们一起杀掉。
# 正确做法：起任务时记 pid + 启动时刻，判活用 kill -0 再比启动时刻（dm/verify/merge_running.sh 判合入在跑、
# dm/ios/tools/devlock.sh 判设备锁持有者都是这套）；要收进程只按自己记下的 pid。
# 只是把进程列出来给人看、不拿结果做判断的，在那一行末尾写注释「只列不判」豁免。
# 用法：namematch_scan.py <目录树>   列出每处「文件:行号:片段」；有命中时退出码 1。整行注释与行尾注释里的不算。
# Flags as red any script that uses pgrep -f / pkill -f to decide "whether some task is running" by command line text, or reaps processes by name.
# Text matching hits any process whose command line contains the word: the waiting task itself (until ! pgrep -f merge.sh matched itself and waited 33 minutes for nothing),
# someone else's task with the same name, strings in an editor / grep / ssh; pkill -f also kills them all.
# Correct approach: record pid + start time when starting a task, check liveness with kill -0 and then compare the start time (dm/verify/merge_running.sh for merge-running checks and
# dm/ios/tools/devlock.sh for device lock holders both use this); reap processes only by pids you recorded yourself.
# Lines that only list processes for people to read without deciding on the result are exempted by the comment "只列不判" at the end of that line.
# Usage: namematch_scan.py <directory tree>   lists each "file:line:snippet"; exits 1 when there are hits. Matches in full-line comments and trailing comments do not count.
import os, re, sys
T = sys.argv[1]
RX = re.compile(rb'\b(pgrep|pkill)\b[^|;&\n"\']*?\s(-[A-Za-z0-9]*f[A-Za-z0-9]*|--full)(?=\s|$)')
OK = '只列不判'.encode()
def is_script(p):
    if p.endswith(('.sh', '.py', '.zsh')): return True
    if '.' in os.path.basename(p): return False
    try:
        with open(p, 'rb') as f: h = f.readline()
        return h.startswith(b'#!') and (b'sh' in h or b'python' in h)
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
        if os.path.islink(p) or not is_script(p) or os.path.abspath(p) == os.path.abspath(__file__): continue
        with open(p, 'rb') as fh: L = fh.read().split(b'\n')
        for i, l in enumerate(L):
            if OK in l: continue
            c = code_part(l)
            for m in RX.finditer(c):
                print('%s:%d:%s' % (os.path.relpath(p, T), i + 1, c[max(0, m.start() - 20):m.end() + 40].decode('utf-8', 'replace').strip())); n += 1
print('NAMEMATCH %s：按命令行文本 pgrep -f / pkill -f %d 处（改用 pid + 启动时刻，见 dm/verify/merge_running.sh、dm/ios/tools/devlock.sh）' % ('OK' if n == 0 else 'RED', n))
sys.exit(1 if n else 0)
