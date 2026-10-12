import pickle,glob,collections,sys,struct
from capstone import *
img=open('main.bin','rb').read()
r=[x for f in glob.glob('x18_*.pkl') for x in pickle.load(open(f,'rb'))]
md=Cs(CS_ARCH_ARM64,CS_MODE_ARM);md.detail=True
c=collections.Counter();ex={}
for a,sz,*_ in r:
    for ins in md.disasm(img[a:a+sz],a):
        r_,w_=ins.regs_access()
        names={ins.reg_name(x) for x in list(r_)+list(w_)}
        if not names&{'x18','w18'}: continue
        m=ins.mnemonic
        usesp='sp' in names or 'wsp' in names
        wb='!' in ins.op_str or (m.startswith(('ldr','str','ldp','stp')) and '], #' in ins.op_str)
        k=m.split('.')[0]
        if m in('br','blr','cbz','cbnz','tbz','tbnz','adr','adrp') or (m=='ldr' and '[' not in ins.op_str): k='CTRL/PCREL:'+m
        elif usesp and wb: k='SP+WB:'+m
        elif usesp and 'sp' in {ins.reg_name(x) for x in w_}: k='SPWRITE:'+m
        elif usesp: k='SPBASE'
        else: k='plain'
        c[k]+=1; ex.setdefault(k,f'{ins.address:x} {m} {ins.op_str}')
for k,v in c.most_common(): print(v,k,ex[k])
