# 执行原ARM编解码；只有libm调用用显式f32替身。不是速度或游戏停止判据。
# Executes the original ARM codec; only libm calls use explicit f32 stand-ins. Not a criterion for speed or for the game stopping.
import math, struct, hashlib, sys
from pathlib import Path
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *
P=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).with_name('build')/'main.orig.bin'
b=P.read_bytes(); u=Uc(UC_ARCH_ARM64,UC_MODE_ARM)
u.mem_map(0,0x4000000); u.mem_write(0,b)
u.mem_map(0x7000000,0x20000)
STOP,VEC,OUT,AMP,FLAG=0x7000000,0x7001000,0x7002000,0x7002010,0x7002020
bits=lambda v:struct.unpack('<I',struct.pack('<f',v))[0]
flt=lambda v:struct.unpack('<f',struct.pack('<I',v))[0]
def hook(u,a,n,d):
 if a==STOP: u.emu_stop(); return
 fn={0x1b77270:lambda:math.atan2(flt(u.reg_read(UC_ARM64_REG_S0)),flt(u.reg_read(UC_ARM64_REG_S1))),
     0x1b77220:lambda:math.fmod(flt(u.reg_read(UC_ARM64_REG_S0)),flt(u.reg_read(UC_ARM64_REG_S1))),
     0x1b77130:lambda:math.cos(flt(u.reg_read(UC_ARM64_REG_S0))),
     0x1b77120:lambda:math.sin(flt(u.reg_read(UC_ARM64_REG_S0)))}.get(a)
 if fn:
  u.reg_write(UC_ARM64_REG_S0,bits(fn())); u.reg_write(UC_ARM64_REG_PC,u.reg_read(UC_ARM64_REG_LR))
u.hook_add(UC_HOOK_CODE,hook)
def call(pc):
 u.reg_write(UC_ARM64_REG_SP,0x701fff0);u.reg_write(UC_ARM64_REG_LR,STOP)
 u.emu_start(pc,STOP+4,count=3000)
 assert u.reg_read(UC_ARM64_REG_PC)==STOP
n=0
# 期望先写死：四轴方向10bit=512/768/0/256；幅度5bit=0/0/0/7/15/31/31。
# Expectations hard-coded first: four-axis direction 10bit=512/768/0/256; magnitude 5bit=0/0/0/7/15/31/31.
for dx,dy,direction in [(1,0,512),(0,1,768),(-1,0,0),(0,-1,256)]:
 packets=[]
 for amp,mag in [(-1,0),(0,0),(1/64,0),(.25,7),(.5,15),(1,31),(2,31)]:
  pair=[]
  for run in [0,1]:
   u.mem_write(VEC,struct.pack('<ff',dx,dy));u.reg_write(UC_ARM64_REG_X0,VEC);u.reg_write(UC_ARM64_REG_X1,run);u.reg_write(UC_ARM64_REG_S0,bits(amp));call(0x4cd4b0)
   packet=u.reg_read(UC_ARM64_REG_W0);want=direction|(mag<<10)|(run<<15)
   assert packet==want,('encode',dx,dy,amp,run,hex(packet),hex(want))
   pair.append(packet)
   u.mem_write(OUT,b'\xff'*0x30);u.reg_write(UC_ARM64_REG_X0,packet);u.reg_write(UC_ARM64_REG_X1,OUT);u.reg_write(UC_ARM64_REG_X2,AMP);u.reg_write(UC_ARM64_REG_X3,FLAG);call(0x4cd5f0)
   x,y=struct.unpack('<ff',u.mem_read(OUT,8));a=struct.unpack('<f',u.mem_read(AMP,4))[0];f=u.mem_read(FLAG,1)[0]
   assert abs(x-dx)<1e-6 and abs(y-dy)<1e-6,('direction',dx,dy,x,y)
   assert abs(a-mag/31)<1e-7 and f==run,('decode',amp,mag,a,f,run)
   n+=1
  assert pair[0]^pair[1]==0x8000
 print(f'轴({dx},{dy}) 方向码{direction}：7幅度×2走跑位，编解码全同')
print('原映像SHA256',hashlib.sha256(b).hexdigest())
print(f'ORIGINAL_MOVE_CODEC OK {n}/{n}；28对只差bit15，零/微幅量化保持0；未测速度/停杆/真实动作')
