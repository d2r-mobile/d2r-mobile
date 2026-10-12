import pickle,struct,bisect,sys
from capstone import *
img=open('sdk.bin','rb').read();md=Cs(CS_ARCH_ARM64,CS_MODE_ARM);md.detail=True
syms=pickle.load(open('sdk_syms.pkl','rb'))
funcs=sorted((v,k) for k,(v,s) in syms.items() if v<0x58d930)
addrs=[f[0] for f in funcs]
def fname(a):
    i=bisect.bisect_right(addrs,a)-1; return f'{funcs[i][1]}+{a-funcs[i][0]:#x}'
m=struct.unpack_from('<I',img,4)[0]; dyn=m+struct.unpack_from('<i',img,m+4)[0]
t={};o=dyn
while True:
    a,v=struct.unpack_from('<qQ',img,o);o+=16
    if a==0:break
    t.setdefault(a,v)
sym,strt=t[6],t[5]
got={}
for i in range(0,t[2],24):
    off,info,add=struct.unpack_from('<QQq',img,t[23]+i); si=info>>32
    nm=struct.unpack_from('<I',img,sym+si*24)[0]; got[off]=img[strt+nm:img.index(b'\0',strt+nm)].decode()
plt={}
for o in range(0,0x58d930,4):
    w=struct.unpack_from('<I',img,o)[0]
    if w&0x9F00001F==0x90000010:
        i2=list(md.disasm(img[o:o+16],o))
        if len(i2)==4 and i2[3].mnemonic=='br' and i2[1].mnemonic=='ldr':
            page=i2[0].operands[1].imm; off=i2[1].operands[1].mem.disp
            if page+off in got: plt[o]=got[page+off]
want={a:n for a,n in plt.items() if n in sys.argv[1:]}
print(want)
for o in range(0,0x58d930,4):
    w=struct.unpack_from('<I',img,o)[0]
    if w>>26==0x25 or w>>26==0x05:
        tgt=o+(((w&0x3ffffff)^0x2000000)-0x2000000)*4
        if tgt in want: print('call',want[tgt],'from',fname(o))
pickle.dump(plt,open('sdk_plt.pkl','wb'))
