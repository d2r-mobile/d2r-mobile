# 逐通道 GPU：按帧内序号平均时间线增量 inc_ms；与 @gpu_ms 对照（列见 nvn_mtl.m DM_NVN_PASSGPU 注释）
# Per-pass GPU: average the timeline increment inc_ms by in-frame index; compare against @gpu_ms (for the columns see the DM_NVN_PASSGPU comment in nvn_mtl.m)
import sys, collections, statistics as S
X=sys.argv[1]; F0=int(sys.argv[2]) if len(sys.argv)>2 else 300; N=int(sys.argv[3]) if len(sys.argv)>3 else 10
rows=[l.rstrip('\n').split('\t') for l in open(f'{X}/passgpu.tsv')][1:]
inc=collections.defaultdict(list); per=collections.defaultdict(float); meta={}
for r in rows:
    f=int(r[0])
    if f<=F0: continue
    inc[int(r[1])].append(float(r[4])); per[f]+=float(r[4]); meta[int(r[1])]=r
g={}
try:
    for l in open(f'{X}/frames.tsv'):
        p=l.rstrip('\n').split('\t')
        if len(p)==3 and p[0].isdigit() and p[1]=='@gpu_ms': g[int(p[0])]=float(p[2])
except FileNotFoundError: pass
c=[f for f in per if f in g]
print('帧数 %d；每帧增量之和 p50 %.2f / p95 %.2f ms%s'%(len(per),S.median(per.values()),sorted(per.values())[int(len(per)*.95)],'；同帧 @gpu_ms p50 %.2f ms'%S.median(g[f] for f in c) if c else ''))
tot=sum(S.mean(v) for v in inc.values())
print('前列（帧内序号：增量均值 ms，占比 ｜ 尺寸 颜色格式 深度格式 读入 绘制数 种类）')
for k in sorted(inc,key=lambda k:-S.mean(inc[k]))[:N]:
    r=meta[k]; m=S.mean(inc[k]); print('  #%d：%.3f ms %4.1f%% ｜ %sx%s fmt%s d%s %s %s 绘制 %s'%(k,m,100*m/tot,r[5],r[6],r[7],r[8],r[9],r[10],r[2]))
print('通道数 / 帧：%d；合计 %.2f ms'%(len(inc),tot))
