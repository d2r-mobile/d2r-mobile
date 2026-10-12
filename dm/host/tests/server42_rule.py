# disabled_bit_guard只测原位资格；人工局部内存fixture，不证明存活player控制锁的合法来源。
# 执行原完整42 handler的输入合法性与分派；decoder/下游movement/致命日志为显式外围替身。
# disabled_bit_guard only tests in-place eligibility; a hand-made local memory fixture, which does not prove the legitimate source of the control lock of a living player.
# Executes the input validity checks and dispatch of the original full 42 handler; the decoder/downstream movement/fatal log are explicit peripheral stand-ins.
import sys,struct
from pathlib import Path
from unicorn import *
from unicorn.arm64_const import *
b=Path(sys.argv[1]).read_bytes();u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0,0x4000000);u.mem_write(0,b);u.mem_map(0x7000000,0x20000)
G,U,D,C,P,PK,STOP=0x7001000,0x7002000,0x7003000,0x7004000,0x7005000,0x7006000,0x701f000
ctx={}
def hook(u,a,n,d):
 if a==STOP:ctx['returned']=True;u.emu_stop();return
 if a==0x4cd5f0:
  v=u.reg_read(UC_ARM64_REG_W0);u.mem_write(u.reg_read(UC_ARM64_REG_X1),struct.pack('<ff',1,0));u.mem_write(u.reg_read(UC_ARM64_REG_X2),struct.pack('<f',1));u.mem_write(u.reg_read(UC_ARM64_REG_X3),bytes([v>>15]));u.reg_write(UC_ARM64_REG_PC,u.reg_read(UC_ARM64_REG_LR))
 elif a==0x6cd670:ctx['move']=u.reg_read(UC_ARM64_REG_W2);u.reg_write(UC_ARM64_REG_W0,0);u.reg_write(UC_ARM64_REG_PC,u.reg_read(UC_ARM64_REG_LR))
 elif a==0xde1850:ctx['fatal']=True;u.emu_stop()
u.hook_add(UC_HOOK_CODE,hook);bad=0;n=0
for bit in [0,1]:
 for case in ['valid','badlen','dead0','dead17','disabled_bit_guard','far','nonplayer']:
  ctx.clear();u.mem_write(G,b'\0'*0x1000);u.mem_write(U,b'\0'*0x400);u.mem_write(D,b'\0'*0x200);u.mem_write(C,b'\0'*0x1000);u.mem_write(P,b'\0'*0x300)
  u.mem_write(U,struct.pack('<I',int(case=='nonplayer')));u.mem_write(U+12,struct.pack('<I',0 if case=='dead0'else 17 if case=='dead17'else 1));u.mem_write(U+0x10,struct.pack('<Q',D));u.mem_write(D+0xf0,struct.pack('<Q',C));u.mem_write(U+0x38,struct.pack('<Q',P));u.mem_write(P+2,struct.pack('<H',5826));u.mem_write(P+6,struct.pack('<H',4520));u.mem_write(U+0x126,bytes([int(case=='disabled_bit_guard')]));u.mem_write(G+0x170,struct.pack('<I',421));u.mem_write(PK,struct.pack('<BHHH',0x42,512|(31<<10)|(bit<<15),5926 if case=='far'else 5826,4520))
  for r,v in [(UC_ARM64_REG_X0,G),(UC_ARM64_REG_X1,U),(UC_ARM64_REG_X2,PK),(UC_ARM64_REG_X3,6 if case=='badlen'else 7),(UC_ARM64_REG_SP,0x701fff0),(UC_ARM64_REG_LR,STOP)]:u.reg_write(r,v)
  u.emu_start(0x6dc270,STOP+4,count=5000)
  wantmove=3 if case=='valid'else None;wantstatus=3 if case=='badlen'else 1 if case in ['dead0','dead17','disabled_bit_guard','far']else 0
  ok=(ctx.get('fatal',False) or ctx.get('returned',False)) and ctx.get('move')==wantmove and (ctx.get('fatal',False)==(case=='nonplayer')) and (case=='nonplayer'or u.reg_read(UC_ARM64_REG_W0)==wantstatus)
  n+=1
  if not ok:bad+=1;print('FAIL',bit,case,'期望move',wantmove,'→真',ctx,'status',u.reg_read(UC_ARM64_REG_W0))
print(f'SERVER42_RULE {n}项，失败{bad}；真实RET到STOP/原致命停机判据；bit0/1合法请求Run3，长度/死亡/禁用位/坐标/非player原检查；不含下游真实速度')
sys.exit(bool(bad))
