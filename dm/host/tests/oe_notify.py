# 直接跑原映像 main+e10d30；只替换系统查询与resize回调，不重写消费函数。
# 手写期望：消息循环结束focus/op/perf各为当前值；目标尺寸对应性能档；相同目标仅resize一次。
# Runs the original image main+e10d30 directly; only the system queries and the resize callback are replaced, the consumer function is not rewritten.
# Hand-written expectations: at the end of the message loop focus/op/perf each hold the current value; the target size corresponds to the performance mode; the same target is resized only once.
import struct,itertools,hashlib,sys
from pathlib import Path
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import *
P=Path(sys.argv[1]);b=P.read_bytes()
u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(0,0x4000000);u.mem_write(0,b);u.mem_map(0x7000000,0x2000);u.mem_map(0x7100000,0x10000)
if sys.argv[2:] == ['--negative']:
 u.mem_write(0xe10e24,bytes.fromhex('1f2003d5')) # 只在负对照中移除尺寸相同跳过，重复通知会重复resize | only in the negative control is the same-size skip removed, so repeated notifications resize repeatedly
 print('NEGATIVE：本地原函数副本e10e24 b.eq→nop，不改资源正本')
u.mem_write(0x3211718,struct.pack('<Q',0x7001001));u.mem_write(0x7001000,struct.pack('<Q',0x7001100));u.mem_write(0x7001110,struct.pack('<Q',0x7001200))
state={}
def hook(u,a,n,d):
 if a==0x7001800:u.emu_stop();return
 if a==0x1b77890:
  q=state['q'];out=u.reg_read(UC_ARM64_REG_X0)
  if q:u.mem_write(out,struct.pack('<I',q.pop(0)));u.reg_write(UC_ARM64_REG_X0,1)
  else:u.reg_write(UC_ARM64_REG_X0,0)
 elif a==0x1b778a0:u.reg_write(UC_ARM64_REG_X0,state['mode'])
 elif a==0x1b778b0:u.reg_write(UC_ARM64_REG_X0,state['focus'])
 elif a==0x1b778c0:u.reg_write(UC_ARM64_REG_X0,state['mode'])
 elif a==0x7001200:
  w=struct.unpack('<Q',u.mem_read(u.reg_read(UC_ARM64_REG_X3),8))[0];h=struct.unpack('<Q',u.mem_read(u.reg_read(UC_ARM64_REG_X4),8))[0]
  state['resize'].append((w,h));u.reg_write(UC_ARM64_REG_X0,0)
 else:return
 u.reg_write(UC_ARM64_REG_PC,u.reg_read(UC_ARM64_REG_LR))
u.hook_add(UC_HOOK_CODE,hook)
def run(q,mode,focus,canvas=(1600,900),target=(0,0)):
 state.update(q=list(q),mode=mode,focus=focus,resize=[])
 u.mem_write(0x211da30,struct.pack('<II',*canvas));u.mem_write(0x211da38,struct.pack('<II',*target))
 for a in [0x315e628,0x315e630,0x32137e8,0x32137ec]:u.mem_write(a,b'\x00'*8)
 u.reg_write(UC_ARM64_REG_SP,0x710fff0);u.reg_write(UC_ARM64_REG_LR,0x7001800)
 try:u.emu_start(0xe10d30,0x7001804,count=20000)
 except Exception:
  print("FAIL PC",hex(u.reg_read(UC_ARM64_REG_PC)),"LR",hex(u.reg_read(UC_ARM64_REG_LR)),"SP",hex(u.reg_read(UC_ARM64_REG_SP)));raise
 assert u.reg_read(UC_ARM64_REG_PC)==0x7001800,'未返回'
 return (struct.unpack('<I',u.mem_read(0x315e628,4))[0],u.mem_read(0x315e630,2),u.mem_read(0x32137e8,1),u.mem_read(0x32137ec,1),struct.unpack('<II',u.mem_read(0x211da38,8)),list(state['resize']))
checks=0
print('原映像SHA256',hashlib.sha256(b).hexdigest())
for focus,mode in [(1,0),(3,1)]:
 for order in itertools.permutations([15,30,31]):
  full=[m for m in order for _ in range(11)] # 33条，确实超过旧32容量；每个消息仍不携带历史值 | 33 messages, really exceeding the old capacity of 32; each message still carries no historical value
  a=run(full,mode,focus);c=run(order,mode,focus);checks+=1
  size=(1280,720) if mode==0 else (1600,900)
  want=(focus,bytes([mode,mode]),bytes([mode]),bytes([mode]),size,[size])
  ok=a==c==want
  print(('✓' if ok else '✗')+f' 顺序{order} focus={focus} mode={mode}：33条原始通知/3条状态通知 → 写回相同且resize恰1次 {c}')
  if not ok:raise SystemExit(1)
for mode in [0,1]:
 size=(1280,720) if mode==0 else (1600,900)
 for name,canvas,target,out,resizes in [('目标已匹配',(1600,900),size,size,[]),('容量不足',(1,1),(777,333),(777,333),[]),('宽度匹配高度不同',(1600,900),(size[0],333),size,[size])]:
  a=run([15]*11+[30]*11+[31]*11,mode,2,canvas,target);c=run([15,30,31],mode,2,canvas,target);checks+=1
  want=(2,bytes([mode,mode]),bytes([mode]),bytes([mode]),out,resizes)
  ok=a==c==want;print(('✓' if ok else '✗')+f' {name} mode={mode} focus=2：33/3条通知写回符合手写期望，resize={len(c[-1])}')
  if not ok:raise SystemExit(1)
print(f'CONSUMER_EQUIV OK {checks}/{checks}：原版ARM消费函数、33→3通知、手写状态/尺寸/resize')
