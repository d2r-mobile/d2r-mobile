# 连拍分析：burst_sad.py <连拍目录>。连拍由触点脚本步骤「burst <帧数> <目录名>」产出（nvn_mtl.m burst_grab），目录在 App 的 Documents 下。
# 每对相邻帧：高通后去掉屏幕中心人物那块，在 ±R 像素内找绝对差最小的整像素位移（环境变量 R，缺省 14；30 帧档每帧位移大，给 26）；
# 同时打印 meta.txt 里记的本地玩家逻辑位移（格）。相位相关在草地这类弱纹理上会读成 0，所以用块匹配
# Burst analysis: burst_sad.py <burst directory>. A burst is produced by the touch-script step "burst <frames> <directory name>" (nvn_mtl.m burst_grab); the directory is under the App's Documents.
# For each pair of adjacent frames: after a high-pass, with the character block at the screen centre removed, find the whole-pixel displacement with the smallest absolute difference within +-R pixels (environment variable R, default 14; at 30 fps the per-frame displacement is larger, use 26);
# the logical displacement of the local player (tiles) recorded in meta.txt is printed alongside. Phase correlation reads 0 on weak textures such as grass, hence block matching
import os,sys,numpy as np
from PIL import Image, ImageFilter
d=sys.argv[1]; L=open(d+'/meta.txt').read().split('\n'); n=int(L[0].split()[-1]); W=int(L[0].split()[1]); L=L[1:n+1]
P=[(float(l.split()[8]),float(l.split()[10])) for l in L] if len(L[0].split())>8 else None
def hp(k):
    a=np.frombuffer(open(f'{d}/f{k:03d}.raw','rb').read(),dtype=np.uint8).reshape(W,W,4)[:,:,:3].mean(2)
    im=Image.fromarray(a.astype(np.uint8)); b=np.asarray(im.filter(ImageFilter.GaussianBlur(6)),dtype=float); return a-b
H=[hp(k) for k in range(n)]; c=W//2; R=14
R=int(os.environ.get("R","14")); mask=np.ones((W,W),bool); mask[c-160:c+120,c-120:c+120]=False; mask[:R+1]=mask[-R-1:]=False; mask[:,:R+1]=mask[:,-R-1:]=False
ys,xs=np.nonzero(mask)
out=[]
for k in range(n-1):
    a=H[k][ys,xs]; best=None
    for dy in range(-R,R+1):
        for dx in range(-R,R+1):
            v=np.abs(a-H[k+1][ys+dy,xs+dx]).mean()
            if best is None or v<best[0]: best=(v,dx,dy)
    out.append((best[1],best[2]))
for k,(dx,dy) in enumerate(out):
    lp='' if not P else ' 逻辑位移 %+.2f,%+.2f'%(P[k+1][0]-P[k][0],P[k+1][1]-P[k][1])
    print(f'{k:3d}→{k+1:3d} 画面位移 {dx:+d},{dy:+d}{lp}')
