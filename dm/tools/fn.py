import struct,collections
img=open('main.bin','rb').read()
m=struct.unpack_from('<I',img,4)[0]
assert img[m:m+4]==b'MOD0',img[m:m+4]
dyn,bs,be,ehs,ehe,mo=[m+x for x in struct.unpack_from('<6i',img,m+4)]
print('eh_frame_hdr',hex(ehs),hex(ehe))
ver,ptrenc,cntenc,tblenc=img[ehs:ehs+4]
# eh_frame_ptr (enc), fde_count (enc), table
def rd(o,enc,base):
    fmt=enc&0xf; app=enc&0x70
    v,sz={0x3:(struct.unpack_from('<I',img,o)[0],4),0xb:(struct.unpack_from('<i',img,o)[0],4),0x4:(struct.unpack_from('<Q',img,o)[0],8)}[fmt]
    if app==0x10: v+=o
    if app==0x30: v+=base
    return v,sz
o=ehs+4
_,s=rd(o,ptrenc,ehs);o+=s
n,s=rd(o,cntenc,ehs);o+=s
starts=[]
for i in range(n):
    a,s=rd(o,tblenc,ehs);o+=s
    b,s=rd(o,tblenc,ehs);o+=s
    starts.append(a)
starts=sorted(set(starts))
print('FDE functions',len(starts),'text end',hex(0x1b786a0))
import pickle;pickle.dump(starts,open('starts.pkl','wb'))
# x18 / x16/x17 usage per function via capstone regs_access is slow; quick: decode register fields roughly with capstone detail on all text
