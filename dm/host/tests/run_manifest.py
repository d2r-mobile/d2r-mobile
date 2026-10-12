# 新规则产物独立登记核验；不执行游戏、不信任改写器自己的摘要。
# Independent registration check of the new rule's build output; does not execute the game and does not trust the rewriter's own digest.
import sys,struct,hashlib
from pathlib import Path
D=Path(sys.argv[1]); b=(D/'main.patched.bin').read_bytes(); orig=Path(sys.argv[2]).read_bytes()
headers={};blocks=[]
try:
 for line in (D/'main.rules.tsv').read_text().splitlines():
  if line.startswith('# '):k,v=line[2:].split('\t',1);headers[k]=v
  else:blocks.append(line.split('\t'))
 assert headers['RULESET']=='touch-run-v4'
 assert headers['BUILD_ID']=='db0507f36b84200ac05f75d76ed5026f'
 assert headers['ORIGINAL_SHA256']==hashlib.sha256(orig).hexdigest()=='6843ab6909f96e1558b4b73b0fa807f3873409120cf3e1ef8a86dbebb275ee27'
 assert headers['IMAGE_SHA256']==hashlib.sha256(b).hexdigest()
 expected={0x2c620c:0x52800029,0x2c6210:0xb90e5109,0x369a90:None,0x369980:0xd503201f,0x2b2c74:0xd503201f,0x2b2dc0:0xd503201f,0x2b30fc:0x52800021,0x2b3104:0x52800021,0x6dc2c4:0x52800062,0x6dce90:0xd503201f,0x6dcec0:None,0x6c6648:None,0x6c6290:None,0x4ac368:None,0x6c6580:None}
 cuts={}
 for line in (D/'main.cuts.tsv').read_text().splitlines():
  if line.startswith('#'):continue
  off,old,new,scope,why=line.split('\t',4)
  if why.startswith('touch-run-v4：'):cuts[int(off,16)]=(old,new,scope)
 assert set(cuts)==set(expected),('剪口集合',set(cuts)^set(expected))
 def w(at):return struct.unpack_from('<I',b,at)[0]
 def branch(at):
  v=w(at);assert v&0xfc000000==0x14000000;imm=v&0x3ffffff
  return at+4*((imm^0x2000000)-0x2000000)
 for off,newword in expected.items():
  old,new,scope=cuts[off];assert scope=='全';assert orig[off:off+4]==bytes.fromhex(old);assert b[off:off+4]==bytes.fromhex(new)
  if newword is not None:assert w(off)==newword
 assert branch(0x369a90)==0x369900 and branch(0x6dcec0)==0x6dce50
 assert len(blocks)==4
 fixheaders={}
 for line in (D/'main.fixups.tsv').read_text().splitlines():
  if line.startswith('# '):k,v=line[2:].split('\t');fixheaders[k]=int(v,0)
 po=fixheaders['PATCH_OFF'];seen=set()
 for off,raw,sha,why in blocks:
  off=int(off,16);body=bytes.fromhex(raw);assert po<=off<len(b) and off+len(body)<=len(b);assert b[off:off+len(body)]==body;assert hashlib.sha256(body).hexdigest()==sha
  src=next(s for s in [0x6c6648,0x6c6290,0x4ac368,0x6c6580] if branch(s)==off);assert src not in seen;seen.add(src)
  if src==0x6c6580:
   assert len(body)==24 and w(off)==0x35000088 and w(off+4)==0x51000849 and w(off+8)==0x35000049 and w(off+12)==0x52800062 and w(off+16)==0x51000849 and branch(off+20)==0x6c6584
   continue
  assert w(off)==0xb9400268
  assert w(off+4)==(0x34000048 if src==0x4ac368 else 0x35000048)
  if src==0x6c6648:
   assert len(body)==24 and branch(off+8)==0x6c664c and w(off+12)==0x34000040 and branch(off+16)==0x6c664c and branch(off+20)==0x6c6654
  elif src==0x6c6290:
   assert len(body)==20 and branch(off+8)==0x6c62ec and w(off+12)==0x710006df and branch(off+16)==0x6c6294
  else:
   assert len(body)==28 and branch(off+8)==off+16 and branch(off+12)==0x4ac3f4 and w(off+16)==0x34000040 and branch(off+20)==0x4ac3f4 and branch(off+24)==0x4ac36c
 assert seen=={0x6c6648,0x6c6290,0x4ac368,0x6c6580}
 print('RUN_MANIFEST OK 原像/成像hash、buildid、15剪口白名单、4跳板hash/范围/结构/入口B')
except (AssertionError,KeyError,OSError,ValueError,StopIteration) as e:
 print('RUN_MANIFEST FAIL',type(e).__name__,str(e));sys.exit(1)
