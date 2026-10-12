# 锁等待对齐：全线程采样日志（DM_SAMPLE=毫秒:*）里，取「等待线程在某等待点上的样本」，对到「持锁线程同一时刻的样本」，
# 打出持锁线程那一刻的宿主调用链（到第一个游戏帧为止）与次数汇总——看持锁时在干什么。
# 用法：lockalign.py <日志> <起s> <止s> <等待线程 0x…> <等待点前缀，如 main+0x15c8> <持锁线程 0x…>
# 例（⑤ 装载期资源锁）：lockalign.py samp16/host.log 0.347 0.966 0x1005 main+0x15c8 0x1001
# Lock wait alignment: in an all-thread sampling log (DM_SAMPLE=ms:*), take "the samples of the waiting thread at a given wait point" and match them to "the samples of the lock-holding thread at the same moment",
# then print the lock-holding thread's host call chain at that moment (up to the first game frame) with a count summary -- to see what it was doing while holding the lock.
# Usage: lockalign.py <log> <start s> <end s> <waiting thread 0x...> <wait point prefix, e.g. main+0x15c8> <lock-holding thread 0x...>
# Example (⑤ resource lock during loading): lockalign.py samp16/host.log 0.347 0.966 0x1005 main+0x15c8 0x1001
import re, sys, collections
log, t0, t1, wth, wpt, hth = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), sys.argv[4], sys.argv[5], sys.argv[6]
L = open(log, errors='replace').read().splitlines(); S = []
for i, l in enumerate(L):
    m = re.match(r'\[dm\] SAMPLE #\d+ \[([\d.]+)s\] thr=(0x[0-9a-f]+) pc=(\S+) lr=(\S+)', l)
    if m and i + 1 < len(L) and L[i + 1].startswith('[dm] bt:'): S.append((float(m.group(1)), m.group(2), m.group(3), L[i + 1][9:].split()))
W = [s for s in S if s[1] == wth and t0 <= s[0] <= t1 and any(f.startswith(wpt) for f in [s[2]] + s[3][:12])]
H = [s for s in S if s[1] == hth]
agg = collections.Counter()
print(f'{wth} 在 {wpt}* 上等的样本 {len(W)}')
for t, *_ in W:
    n = min(H, key=lambda s: abs(s[0] - t)); chain = []
    for f in [n[2]] + n[3]:
        if f.startswith(('main+', 'sdk+')): break
        chain.append(f.split('+')[0])
    game = [f for f in n[3] if f.startswith('main+')][:4]
    print(f'{t:.3f} 持锁线程@{n[0]:.3f} 宿主链：{" ← ".join(chain[:8])} | 游戏帧 {" ← ".join(game)}')
    for f in set(chain): agg[f] += 1
print('宿主函数出现次数：', agg.most_common(15))
