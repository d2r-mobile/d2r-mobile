# TBDR 读入可省的候选：DM_NVN_PASSDUMP 的 P（附件）与 D（通道首次绘制特征）行。颜色附件读入为候选的条件：
# 首绘是单实例三角形 / 四边形 / 条带、顶点 3–6、剪裁与视口覆盖整个目标、该附件不混合且写掩码满、深度测试关或 ALWAYS、模板关。
# 覆盖是否真为全屏取决于顶点着色器，候选须再用哨兵色实验（读入改成清成哨兵色，比最终画面）证实。
# Candidates for skippable TBDR loads: the P (attachment) and D (first-draw features of a pass) rows of DM_NVN_PASSDUMP. Conditions for a color attachment load to be a candidate:
# the first draw is a single-instance triangle / quad / strip with 3–6 vertices, scissor and viewport cover the whole target, the attachment is not blended and has a full write mask, depth test off or ALWAYS, stencil off.
# Whether coverage is truly fullscreen depends on the vertex shader; candidates must still be confirmed by a sentinel color experiment (change the load into a clear to the sentinel color and compare the final image).
import re, sys, collections
BPP={70:4,71:4,80:4,81:4,90:4,92:4,93:4,115:8,125:16,55:4,25:2,65:4,105:4,103:4,252:4,250:2,260:5,255:4,10:1,20:2,30:2,60:4,53:4}
P=collections.defaultdict(list); D={}; FA=None
for l in open(sys.argv[1]):
    l=l.strip()
    m=re.match(r'\[pass\] F(\d+) P (\d+) (c\d+|d) ser=(\d+) fmt=(\d+) (\d+)x(\d+)(?:.*? lv(\d+))?.*?(clear|load|dontcare)$',l)
    if m:
        f,p,slot,s,fmt,w,h,lv,act=m.groups()
        if FA is None: FA=int(f)
        if int(f)!=FA: continue
        lv=int(lv or 0); P[int(p)].append((slot,int(s),int(fmt),act,max(int(w)>>lv,1)*max(int(h)>>lv,1)*BPP.get(int(fmt),4)))
        continue
    m=re.match(r'\[pass\] F(\d+) D (\d+) prim=(\d+) vc=(\d+) inst=(\d+) blend=(\S+) wm=(\S+) dt=(\d) dw=(\d) df=(\d+) st=(\d) sc=(\d+),(\d+),(\d+),(\d+) tgt=(\d+)x(\d+) vp=([-\d.]+),([-\d.]+),([-\d.]+),([-\d.]+)',l)
    if m and FA is not None and int(m.group(1))==FA:
        g=m.groups(); D[int(g[1])]=dict(prim=int(g[2]),vc=int(g[3]),inst=int(g[4]),blend=int(g[5],16),wm=int(g[6],16),dt=int(g[7]),df=int(g[9]),st=int(g[10]),
            sc=tuple(map(int,g[11:15])),tgt=(int(g[15]),int(g[16])),vp=tuple(map(float,g[17:21])))
ALWAYS=8
tot=cand=0; rows=[]
for p,atts in P.items():
    for slot,s,fmt,act,b in atts:
        if act!='load': continue
        tot+=b
        d=D.get(p); why=[]
        if not d: why.append('无绘制')
        else:
            W,H=d['tgt']
            if d['prim'] not in (4,5,7) or not 3<=d['vc']<=6 or d['inst']>1: why.append('非单个全屏图元')
            if d['sc']!=(0,0,W,H): why.append('剪裁非整屏')
            if d['vp'][:2]!=(0,0) or d['vp'][2]<W or d['vp'][3]<H: why.append('视口非整屏')
            if slot=='d': why.append('深度读入（不判）')
            else:
                i=int(slot[1:])
                if d['blend']>>i&1: why.append('混合')
                if (d['wm']>>(4*i))&0xf!=0xf: why.append('写掩码不满')
            if d['dt'] and d['df']!=ALWAYS: why.append('深度测试')
            if d['st']: why.append('模板测试')
        if not why: cand+=b
        rows.append((p,slot,s,fmt,b,'候选' if not why else '/'.join(why)))
print('帧 %d：附件读入 %.1f MB；首绘全屏无依赖的候选 %.1f MB（%.0f%%）'%(FA,tot/1e6,cand/1e6,100*cand/tot if tot else 0))
c=collections.Counter(r[5] for r in rows)
for k,v in c.most_common(): print('  %-28s %3d 个  %.1f MB'%(k,v,sum(r[4] for r in rows if r[5]==k)/1e6))
for r in rows:
    if r[5]=='候选': print('  候选 通道 %d %s ser=%d fmt%d %.1f MB'%(r[0],r[1],r[2],r[3],r[4]/1e6))
