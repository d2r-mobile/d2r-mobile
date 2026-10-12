import pickle,struct,sys
from capstone import *
from capstone.arm64 import *
img=open('main.bin','rb').read()
starts=pickle.load(open('starts.pkl','rb'))
TEXT=0x1b786a0
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM);md.detail=True
lo,hi=int(sys.argv[1]),int(sys.argv[2])
res=[]
for i in range(lo,min(hi,len(starts))):
    a=starts[i]; b=starts[i+1] if i+1<len(starts) else TEXT
    if a>=TEXT: continue
    used=set();x18=0;n=0;bad=0
    for ins in md.disasm(img[a:b],a):
        n+=1
        try: r,w=ins.regs_access()
        except Exception: bad+=1;continue
        for reg in list(r)+list(w):
            nm=ins.reg_name(reg)
            if nm and nm[0] in 'xw' and nm[1:].isdigit(): used.add(int(nm[1:]))
            elif nm in('fp',):used.add(29)
            elif nm in('lr',):used.add(30)
        if 18 in used and x18==0: pass
        x18+=sum(1 for reg in list(r)+list(w) if ins.reg_name(reg) in('x18','w18'))
    if x18:
        free_cs=[k for k in range(9,18) if k not in used]
        free_ce=[k for k in range(19,29) if k not in used]
        res.append((a,b-a,n,x18,len(free_cs),len(free_ce),bad))
pickle.dump(res,open(f'x18_{lo}.pkl','wb'))
