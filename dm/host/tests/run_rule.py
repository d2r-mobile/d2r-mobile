# 新规则的局部原ARM契约；不代替速度/真实动作矩阵。
# Local original-ARM contract of the new rule; does not replace the speed/real action matrix.
import sys,struct,hashlib
from pathlib import Path
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *
orig=Path(sys.argv[1]).read_bytes(); candidate=Path(sys.argv[2]).read_bytes()
U,P,R,A,L=0x7001000,0x7002000,0x7003000,0x7004000,0x7005000
fails=[]; total=0
class Machine:
 def __init__(self,blob):
  self.u=u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0,0x4000000);u.mem_write(0,blob);u.mem_map(0x7000000,0x20000)
  for addr,v in [(U+0x38,P),(P,R),(P+0x20,R),(R+0x18,A),(A+0x90,L)]:u.mem_write(addr,struct.pack('<Q',v))
  self.sta=0;self.calls=[];self.stop=0
  u.hook_add(UC_HOOK_CODE,self.hook)
 def hook(self,u,a,n,d):
  if a==self.stop:u.emu_stop();return
  if a==0x5b9290:self.calls.append(('get',10));u.reg_write(UC_ARM64_REG_X0,self.sta)
  elif a==0x5a13a0:u.reg_write(UC_ARM64_REG_X0,0)
  elif a==0x5bc5a0:
   assert u.reg_read(UC_ARM64_REG_W1)==10
   v=u.reg_read(UC_ARM64_REG_W2);v=(v^0x80000000)-0x80000000
   self.calls.append(('add',v));self.sta+=v
  elif a==0x5bc1f0:
   assert u.reg_read(UC_ARM64_REG_W1)==10
   self.calls.append(('set',u.reg_read(UC_ARM64_REG_W2)));self.sta=u.reg_read(UC_ARM64_REG_W2)
  elif a==0x6c6550 and self.stop==0x6c62ec:self.calls.append(('downgrade',u.reg_read(UC_ARM64_REG_W2)))
  elif a==0x45f9d0:u.reg_write(UC_ARM64_REG_X0,U)
  elif a==0x460fd0:
   v=u.reg_read(UC_ARM64_REG_W1);self.calls.append(('mode',v));u.mem_write(U+0xc,struct.pack('<I',v));u.reg_write(UC_ARM64_REG_X0,0)
  elif a in [0x4bbc50,0x5ca8a0]:
   v=u.reg_read(UC_ARM64_REG_W1);self.calls.append(('mode',v));u.mem_write(U+0xc,struct.pack('<I',v))
  elif a==0x2ceab0:self.calls.append(('packet',u.mem_read(u.reg_read(UC_ARM64_REG_X0),1)[0]))
  elif a==0x5a7510:pass
  else:return
  u.reg_write(UC_ARM64_REG_PC,u.reg_read(UC_ARM64_REG_LR))
 def go(self,start,stop):
  self.stop=stop;self.u.reg_write(UC_ARM64_REG_SP,0x701fff0)
  self.u.emu_start(start,stop+4,count=3000)
  assert self.u.reg_read(UC_ARM64_REG_PC)==stop,hex(self.u.reg_read(UC_ARM64_REG_PC))
 def selector(self,typ,town,sta,mode):
  u=self.u;self.sta=sta;self.calls=[];u.mem_write(U,struct.pack('<I',typ));u.mem_write(L+0x1f8,struct.pack('<I',1 if town else 2))
  u.reg_write(UC_ARM64_REG_X0,0x7006000);u.reg_write(UC_ARM64_REG_X1,U);u.reg_write(UC_ARM64_REG_X2,mode)
  self.go(0x6c6550,0x6c6678);return u.reg_read(UC_ARM64_REG_W22)
 def drain(self,typ,sta):
  self.sta=sta;self.calls=[];u=self.u;u.mem_write(U,struct.pack('<I',typ));u.reg_write(UC_ARM64_REG_X19,U);u.reg_write(UC_ARM64_REG_X20,0x7006000);u.reg_write(UC_ARM64_REG_W22,2)
  self.go(0x6c6290,0x6c62ec);return self.sta,list(self.calls)
def check(label,actual,want):
 global total
 total+=1
 if actual!=want:fails.append((label,actual,want))
a,b=Machine(orig),Machine(candidate)
for typ in [0,1,2,3,4,6]:
 for town in [False,True]:
  for sta in [0,1,22272]:
   for mode in [1,2,3,6,7,10,17,19]:
    old=a.selector(typ,town,sta,mode)
    want=3 if typ==0 and mode in (2,3) else old
    check(f'select type={typ} town={town} sta={sta} mode={mode}',b.selector(typ,town,sta,mode),want)
for typ in [0,1,2,3,4,6]:
 for sta in [0,1,22272]:
  old=a.drain(typ,sta);want=(sta,[]) if typ==0 else old
  check(f'cost type={typ} sta={sta}',b.drain(typ,sta),want)
for pref in [0,1]:
 for hyst in [0,1]:
  for latch in [0,1]:
   u=b.u;u.reg_write(UC_ARM64_REG_W10,pref);u.reg_write(UC_ARM64_REG_W8,hyst);u.reg_write(UC_ARM64_REG_W9,latch)
   b.go(0x2b30e8,0x2b3108);check(f'client select {pref}/{hyst}/{latch}',u.reg_read(UC_ARM64_REG_W1),1)
u=b.u;b.go(0x2c6208,0x2c6214);check('initial run preference',struct.unpack('<I',u.mem_read(0x2d20e50,4))[0],1)
# 构建剪口入口转向检验不宣称完整On执行。
# The build cut entry redirection check does not claim full On execution.
for start,target in [(0x369a90,0x369900),(0x6dcec0,0x6dce50)]:
 u=b.u;u.reg_write(UC_ARM64_REG_SP,0x701fff0);u.emu_start(start,0x4000000,count=1);check(f'off redirect {start:x}',u.reg_read(UC_ARM64_REG_PC),target)
# 真实On/Off函数完整执行；lookup/stat/动画切换/发包为显式外围替身。
# The real On/Off functions are executed in full; lookup/stat/animation switch/packet send are explicit peripheral stand-ins.
for typ in [0,1]:
 for mode in [0,1,2,3,4,5,6,7,10,17,19]:
  for sta in [0,1,22272]:
   for entry in [0x369900,0x369a90,0x6dce50,0x6dcec0]:
    u=b.u;b.sta=sta;b.calls=[];u.mem_write(U,struct.pack('<I',typ));u.mem_write(U+0xc,struct.pack('<I',mode))
    u.mem_write(0x2d20ba1,b'\0');u.mem_write(0x2d20e50,struct.pack('<II',0,0));u.reg_write(UC_ARM64_REG_X1,U);u.reg_write(UC_ARM64_REG_W3,1);u.reg_write(UC_ARM64_REG_LR,0x7000100)
    b.go(entry,0x7000100);want=3 if typ==0 and mode==2 else mode
    actual=struct.unpack('<I',u.mem_read(U+0xc,4))[0]
    check(f'toggle entry={entry:x} type={typ} mode={mode} sta={sta}',actual,want)
    packets=[v for k,v in b.calls if k=='packet']
    wp=[0x49] if entry<0x600000 and typ==0 and mode==2 else []
    check(f'toggle packets entry={entry:x} type={typ} mode={mode} sta={sta}',packets,wp)
print('ORIGINAL_SHA256',hashlib.sha256(orig).hexdigest());print('CANDIDATE_SHA256',hashlib.sha256(candidate).hexdigest())
for label,actual,want in fails[:30]:print(f'FAIL {label}: 期望{want} → 真结果{actual}')
print(f'RUN_RULE_LOCAL {total}项，失败{len(fails)}；选择器/扣除窗/编码选择/偏好/入口转向；非完整动作/速度判据')
sys.exit(bool(fails))
