# 通用移动分派索引点的独立规则测试；入口已经过原null/type5门，终点为明确替身，不验证真实路径。
# Independent rule test of the generic movement dispatch index point; the entry has already passed the original null/type5 gate, the end point is an explicit stand-in, and the real path is not verified.
import sys,struct
from pathlib import Path
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *
u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0,0x4000000);u.mem_write(0,Path(sys.argv[1]).read_bytes());u.mem_map(0x7000000,0x10000);ctx={};bad=0;n=0
G,UNIT,SP=0x7001000,0x7002000,0x700f000
def hook(u,a,n,d):
 if a==0x6c6584:ctx['arrived']=True;u.emu_stop()
u.hook_add(UC_HOOK_CODE,hook)
for kind in [0,1,2,3,4,5,6,99]:
 for mode in [0,1,2,3,5,6,10,0xffffffff]:
  ctx.clear();u.mem_write(UNIT,struct.pack('<I',kind))
  for r,v in [(UC_ARM64_REG_X0,G),(UC_ARM64_REG_X19,UNIT),(UC_ARM64_REG_W8,kind),(UC_ARM64_REG_W2,mode),(UC_ARM64_REG_SP,SP),(UC_ARM64_REG_NZCV,0xa0000000)]:u.reg_write(r,v)
  u.emu_start(0x6c6580,0x4000000,count=100)
  want=3 if kind==0 and mode==2 else mode
  ok=ctx.get('arrived',False) and u.reg_read(UC_ARM64_REG_W2)==want and u.reg_read(UC_ARM64_REG_W9)==((want-2)&0xffffffff) and u.reg_read(UC_ARM64_REG_W8)==kind and u.reg_read(UC_ARM64_REG_X0)==G and u.reg_read(UC_ARM64_REG_X19)==UNIT and u.reg_read(UC_ARM64_REG_SP)==SP and u.reg_read(UC_ARM64_REG_NZCV)==0xa0000000
  n+=1
  if not ok:bad+=1;print('FAIL type',kind,'request',mode,'期望规范',want,'→真',u.reg_read(UC_ARM64_REG_W2),'arrived',ctx.get('arrived',False))
print(f'SERVER_WALK_REQUEST {n}项，失败{bad}；仅type0请求2规范为3并重算索引，其余请求/类型与W0/X19/W8/SP/NZCV保原；终点替身')
sys.exit(bool(bad))
