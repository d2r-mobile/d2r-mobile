#!/bin/bash
# 存档事务确定性回归（HLE 审计第 1–3 条；accept 全档用）：在 cvd5 回放局里按存档写次数注入「被杀」/「写失败」，核已提交目录。
# 用法：save_txn_regress.sh [产物目录]；退出码 0 = 绿。每例一局 host_savecmp.sh（它的对拍结论在注入下必红，不看）。
# cvd5 的提交顺序是确定的：提交 #1（此前存档写 0 次）→ 写 1、2（joe.d2s 与 .meta）→ 提交 #2 → 写 3（共享箱）→ 提交 #3。期望（不跑被测实现即可写出）：
#   被杀在第 1 次写：已提交 = 起始存档，逐文件相同；
#   被杀在第 3 次写：已提交 = 提交 #2 的版本——除 joe/joe.d2s、.meta 外与起始存档逐文件相同（含共享箱），joe.d2s 长度字段 = 文件长、校验和对；
#   写失败在第 1 次：事务 #2 拒绝提交、工作目录丢弃，事务 #3 照常提交 ⇒ 已提交 = 起始存档，只有共享箱 = 真值 save_after 的共享箱；
#                    （cvd5 的共享箱重写前后字节相同，内容区分不了「#3 提交了」与「之后全拒」，另核拒绝之后恰有 1 条「此前存档写 3 次」的成功提交；
#                     内容上的区分由单测 t_fs ④ 证）
#   写失败在第 3 次：事务 #3 拒绝 ⇒ 同「被杀在第 3 次写」。
# 负对照（没有事务 / 不拒绝，这些残缺就进了存档）：被杀例看工作目录 user/0.work 里被截成 0 字节的文件；写失败例看拒绝日志列出的 0 字节文件。
# 截断文件：第 1 次写 = joe/joe.d2s，第 3 次写 = 共享箱。
# Deterministic regression of save transactions (HLE audit items 1–3; used by accept full level): in a cvd5 replay run, injects "killed" / "write failure" by save write count and checks the committed directory.
# Usage: save_txn_regress.sh [build output dir]; exit code 0 = green. Each case is one host_savecmp.sh run (its comparison verdict is necessarily a failure under injection and is ignored).
# The commit order of cvd5 is deterministic: commit #1 (0 save writes before it) → writes 1, 2 (joe.d2s and .meta) → commit #2 → write 3 (shared stash) → commit #3. Expectations (can be written down without running the implementation under test):
#   Killed at write 1: committed = starting save, identical file by file;
#   Killed at write 3: committed = the version of commit #2 -- identical file by file to the starting save except joe/joe.d2s and .meta (shared stash included), the joe.d2s length field = file length, checksum correct;
#   Write failure at write 1: transaction #2 refuses to commit and the working directory is discarded, transaction #3 commits as usual ⇒ committed = starting save, with only the shared stash = the shared stash of the ground-truth save_after;
#                    (the shared stash of cvd5 has the same bytes before and after the rewrite, so content cannot distinguish "#3 committed" from "everything refused afterwards"; additionally check that after the refusal there is exactly 1 successful commit with 「此前存档写 3 次」(3 save writes before it);
#                     the distinction by content is proven by unit test t_fs ④)
#   Write failure at write 3: transaction #3 refuses ⇒ same as "killed at write 3".
# Negative control (without transactions / without refusal these broken files would have gone into the save): for the killed cases look at the files truncated to 0 bytes in the working directory user/0.work; for the write failure cases look at the 0-byte files listed in the refusal log.
# Truncated files: write 1 = joe/joe.d2s, write 3 = shared stash.
set -u
export LC_ALL=C
X=${DM_ROOT:-$HOME/dm-work}; HERE=$(cd "$(dirname "$0")/../.." && pwd)
TR=${TRUTH_RUN:-$X/data/switch/truth_runs/cvd5}; O=${1:-$(bash "$(dirname "$0")/product.sh" path keypage)}
REF=$X/data/switch/save_ref_warlock/0000000000000001/0; AFT=$TR/save_after/0; PY=$X/build/cache/dm/venv/bin/python3
[ -d "$TR" ] && [ -d "$REF" ] || { echo "【红】缺 cvd5 真值局或起始存档"; exit 2; }
bad=0
RUN=$X/run/verify/dm/save_txn/$(date +%m%d_%H%M%S)_$$; mkdir -p "$RUN" || exit 2
echo "SAVE_TXN 现场 ${RUN}；射程 4 例，每例独立存档副本"
# 4 例各起一局宿主、互不相干（局目录带各自进程号、存档各用副本）：一起起跑再逐例判，本项墙钟从 4 局之和降到最慢一局（accept 提速）
# The 4 cases each start one host run and are independent of one another (run directories carry their own pid, each save uses its own copy): start them all together and then judge case by case, so this item's wall clock drops from the sum of 4 runs to the slowest single run (accept speedup)
for c in KILL_AT:1 KILL_AT:3 FS_FAIL:1 FS_FAIL:3; do
  V=${c%%:*}; K=${c##*:}
  env DM_TEST_$V=write:$K bash "$HERE/dm/tools/host_savecmp.sh" "$TR" "$O" > "$RUN/${V}_$K.log" 2>&1 &
done
wait
for c in KILL_AT:1 KILL_AT:3 FS_FAIL:1 FS_FAIL:3; do
  V=${c%%:*}; K=${c##*:}; L=$RUN/${V}_$K.log
  D=$(grep -o '局目录 .*' "$L" | cut -d' ' -f2)
  [ -n "$D" ] && [ -f "$D/host.log" ] || { echo "【红】$c 没跑起来，见 $L"; echo "$c 启动红" >> "$RUN/KEEP"; bad=1; continue; }
  echo "$D" > "$RUN/${V}_$K.dir"
  $PY - "$D" "$REF" "$c" "$K" "$AFT" <<'PY'
import sys,os,hashlib,struct
d,ref,c,k,aft=sys.argv[1],sys.argv[2],sys.argv[3],int(sys.argv[4]),sys.argv[5]
log=open(d+'/host.log',errors='replace').read()
def tree(r):
    o={}
    for root,_,fs in os.walk(r):
        for f in fs: p=os.path.join(root,f); o[os.path.relpath(p,r)]=open(p,'rb').read()
    return o
def d2s_ok(b):
    if len(b)<16: return False
    b=bytearray(b); n,c0=struct.unpack_from('<II',b,8); struct.pack_into('<I',b,12,0); s=0
    for x in b: s=(((s<<1)|(s>>31))+x)&0xffffffff
    return n==len(b) and s==c0
com,a,work=tree(d+'/user/0'),tree(ref),tree(d+'/user/0.work')
hit=('DM_TEST_%s：存档第 %d 次写' % (c.split(':')[0],k)) in log
STASH='ModernSharedStashSoftCoreV2/ModernSharedStashSoftCoreV2.d2i'
fail=c.startswith('FS_FAIL')
exempt={'joe/joe.d2s','joe/joe.d2s.meta'} if k==3 else set()
want=dict(a)
if fail and k==1: want[STASH]=tree(aft)[STASH]; exempt=set()
diff=sorted(f for f in set(want)|set(com) if f not in exempt and want.get(f)!=com.get(f))
ok_d2s=d2s_ok(com.get('joe/joe.d2s',b''))
torn=STASH if k==3 else 'joe/joe.d2s'
if fail:
    rl=[l for l in log.splitlines() if '存档提交拒绝' in l]
    neg=len(rl)==1 and torn in rl[0] and '仍有存档文件开着' not in rl[0]
    if k==1:
        after=log[log.index('存档提交拒绝'):] if rl else ''
        nxt=after.count('（此前存档写 3 次）')==1 and after.count('存档提交 #')==1
        print(f'  下一个事务照常提交（拒绝之后恰 1 条成功提交且此前写 3 次）：{nxt}'); neg=neg and nxt
else: neg=len(work.get(torn,b'x'))==0
ok=hit and not diff and ok_d2s and neg
print(f"{'✓' if ok else '✗'} {c}：注入命中 {hit}；已提交 {len(com)} 个文件，应同期望版 {len(want)-len(exempt)} 个、不同 {len(diff)} {diff[:3]}；joe.d2s 自洽 {ok_d2s}；负对照 {'拒绝日志恰 1 条且列出' if fail else '工作目录'} {torn} 0 字节 {neg}")
sys.exit(0 if ok else 1)
PY
  rc=$?
  if [ "$rc" != 0 ]; then bad=1; echo "$c 判据红；现场保留" >> "$RUN/KEEP"; echo "$c 判据红；现场保留" >> "$D/KEEP"; fi
done
[ $bad = 0 ] && echo "结论：存档事务回归 4/4 绿" || echo "结论：存档事务回归有红"
exit $bad
