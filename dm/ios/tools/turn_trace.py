# 转向平滑逐帧位移表：turn_trace.py <boot.txt> [起始行号 [行数]]。读宿主日志里的「转向帧迹」行（DM_TEST_TURN_TRACE=1，dm_turnsmooth.c），
# 画出来的位置 = 外推坐标 + 误差（1/65536 格）；换成屏幕方向的位移（格）：横 = (dx − dy)，纵 = (dx + dy) / 2（等距投影，纵向压一半）。
# 输出每帧：序号 横位移 纵位移 位移模 | 外推坐标自身的位移模（= 不做平滑时画面的走法）| 误差模 在走 外推拍比例
# Per-frame displacement table for turn smoothing: turn_trace.py <boot.txt> [start line [line count]]. Reads the turn-trace lines in the host log (DM_TEST_TURN_TRACE=1, dm_turnsmooth.c);
# the drawn position = extrapolated coordinates + error (1/65536 tile); converted to displacement in screen directions (tiles): horizontal = (dx - dy), vertical = (dx + dy) / 2 (isometric projection, vertical halved).
# Output per frame: index, horizontal displacement, vertical displacement, displacement magnitude | displacement magnitude of the extrapolated coordinates alone (= how the picture would move without smoothing) | error magnitude, walking, extrapolated tick fraction
import sys,re
rows=[tuple(float(v) for v in m.groups()) for m in (re.search(r'转向帧迹 (-?\d+) (-?\d+) (-?\d+) (-?\d+) (\d) ([\d.]+) ([\d.]+)',l) for l in open(sys.argv[1],encoding='utf-8',errors='replace')) if m]
a=int(sys.argv[2]) if len(sys.argv)>2 else 0; n=int(sys.argv[3]) if len(sys.argv)>3 else len(rows)
def scr(dx,dy): return (dx-dy)/65536,(dx+dy)/131072
for i in range(max(1,a),min(len(rows),a+n)):
    p,q=rows[i-1],rows[i]; sx,sy=scr(q[0]+q[2]-p[0]-p[2],q[1]+q[3]-p[1]-p[3]); rx,ry=scr(q[0]-p[0],q[1]-p[1])
    print('%4d 画面 %+.3f %+.3f 模 %.3f | 不平滑 %+.3f %+.3f 模 %.3f | 误差 %.3f 在走 %d 外推 %.2f'%(i,sx,sy,(sx*sx+sy*sy)**.5,rx,ry,(rx*rx+ry*ry)**.5,((q[2]/65536)**2+(q[3]/65536)**2)**.5,int(q[4]),q[5]))
