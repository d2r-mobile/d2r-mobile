import struct,re
from capstone import *
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
TEXT_END=0x1b786a0
def scan(img,lo,hi):
    out=set()
    for o in range(lo,hi,4):
        for i in md.disasm(img[o:o+4],o):
            if re.search(r'\b[xw]18\b',i.op_str) or 'tpidr_el0' in i.op_str: out.add(o)
    return out
orig=open('main.bin','rb').read(); pat=open('main.patched.bin','rb').read()
A=scan(orig,0,TEXT_END)
done=set(); win=[]
for l in open('sites.tsv'):
    f=l.split()
    if len(f)==3: win.append((int(f[0],16),int(f[2],16)))
    else: done.add(int(f[0],16))
inwin=lambda a:any(lo<=a<hi for lo,hi in win)
miss=sorted(a for a in A if a not in done and not inwin(a))
print('orig text x18/tpidr sites',len(A),'covered',len(done),'windows',len(win),'MISSED',len(miss),[hex(x) for x in miss[:10]])
P=scan(pat,0,TEXT_END); left=sorted(a for a in P if not inwin(a))
print('patched text remaining (outside windows)',len(left),[hex(x) for x in left[:10]])
PO=0x3220000; Q=scan(pat,PO,len(pat)); print('patch area x18/tpidr refs',len(Q))
