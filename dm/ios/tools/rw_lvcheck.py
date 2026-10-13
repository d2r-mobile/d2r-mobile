#!/usr/bin/env python3
# 符文之语清单「需要等级」全量核对：rw_lvcheck.py <DM_TEST_RW_LVDUMP 导出的文件> <原版表目录（含 weapons.txt armor.txt misc.txt runes.txt）>
# 一、整张物品表逐行：宿主读到的需求等级（导出文件，物品码<TAB>值）对原版三张表的 levelreq；
# 二、每个可做成的符文之语：按宿主读数取各符文需求等级的最大值，对按原版表算的同一个量。任何一处不等退出码 1。
# Full check of "required level" in the runeword list: rw_lvcheck.py <file exported by DM_TEST_RW_LVDUMP> <directory of original tables (with weapons.txt armor.txt misc.txt runes.txt)>
# One, the whole item table row by row: the required level the host read (the exported file, item code<TAB>value) against levelreq in the three original tables;
# Two, every runeword that can be made: the maximum of the runes' required levels by the host's readings against the same quantity computed from the original tables. Any mismatch exits with code 1.
import sys,csv,os
dump,d=sys.argv[1],sys.argv[2]
got={}
for l in open(dump,encoding='latin-1'):
    c,v=l.rstrip('\n').split('\t')
    if c: got.setdefault(c,int(v))
def rows(n): return list(csv.DictReader(open(os.path.join(d,n),encoding='latin-1'),delimiter='\t'))
want={}; diff_cols=0
for n in ('weapons.txt','armor.txt','misc.txt'):
    for r in rows(n):
        c=r.get('code','').strip()
        if not c or c in want: continue
        want[c]=int(r['levelreq'] or 0); diff_cols+=(r.get('level') or '0')!=(r['levelreq'] or '0')
bad=[(c,got.get(c),w) for c,w in want.items() if c in got and got[c]!=w]; miss=[c for c in want if c not in got]
print(f'物品表：原版三张表 {len(want)} 个物品码，导出 {len(got)} 个；逐行对上 {sum(1 for c in want if got.get(c)==want[c])}，不等 {len(bad)}，导出里没有的 {len(miss)}；其中物品等级 ≠ 需求等级的物品 {diff_cols} 个（这些行分得出两列）')
for c,g,w in bad[:20]: print(f'  不等：{c} 读到 {g} 原版 {w}')
rb=0; n=0
for r in rows('runes.txt'):
    if r.get('complete')!='1': continue
    rs=[r[f'Rune{i}'] for i in range(1,7) if r.get(f'Rune{i}')]
    if not rs: continue
    n+=1; a=max(got.get(c,-1) for c in rs); b=max(want.get(c,-1) for c in rs)
    if a!=b: rb+=1; print(f'  符文之语 {r["Name"]}（{r.get("*Rune Name","")}）符文 {rs}：读到 {a} 原版 {b}')
print(f'符文之语：可做成的 {n} 个，符文部分的需要等级不等 {rb} 个')
sys.exit(1 if bad or rb else 0)
