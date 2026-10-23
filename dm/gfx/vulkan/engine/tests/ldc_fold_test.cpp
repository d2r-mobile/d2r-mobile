// 直接调用生产的分量下标折叠并解释合成 SPIR-V：翻译器的 LDC 字寻址经只写一次的临时变量，折叠结果须与原值一致。
// Call the production component-index fold on synthetic SPIR-V in the translator's LDC word-addressing form, through
// single-store temporaries, and check every folded value against the original computation.
#include "../xbg_spirv_ldc.h"
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using Module=std::vector<uint32_t>;
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void op(Module &m,uint32_t opcode,std::initializer_list<uint32_t> args){m.push_back((uint32_t(args.size()+1)<<16)|opcode);m.insert(m.end(),args);}
enum : uint32_t {tVoid=1,tFn=2,tInt=4,tUint=5,tPtrInUint=6,tPtrFnInt=7,tPtrFnUint=8,
    c48=10,c32=11,c2=12,c3=13,c1=14,c20=15,c4=16,inBone=30,fMain=20,lEntry=21,
    vT1=40,vT2=41,vT3=42,vT4=43,vT5=44,vMulti=45,vT6=46,vT7=47,vT8=48,vT9=49,vInit=39};
// The skinning form: word = (bone * 48 + 32) >> 2, component = word & 3 (= 0) and (word + 1) & 3 (= 1); a multiplier of 20
// (low bits not enough), a variable stored twice, one with an initializer, and a mask of 4 (needs three known bits).
Module module(){
    Module m{0x07230203,0x00010300,0,100,0};
    op(m,17,{1});op(m,14,{0,1});op(m,15,{0,fMain,0x6e69616d,0,inBone});
    op(m,71,{inBone,30,0});
    op(m,19,{tVoid});op(m,33,{tFn,tVoid});op(m,21,{tInt,32,1});op(m,21,{tUint,32,0});
    op(m,32,{tPtrInUint,1,tUint});op(m,32,{tPtrFnInt,7,tInt});op(m,32,{tPtrFnUint,7,tUint});
    op(m,43,{tInt,c48,48});op(m,43,{tInt,c32,32});op(m,43,{tUint,c2,2});op(m,43,{tInt,c3,3});op(m,43,{tInt,c1,1});op(m,43,{tInt,c20,20});op(m,43,{tInt,c4,4});
    op(m,59,{tPtrInUint,inBone,1});
    op(m,54,{tVoid,fMain,0,tFn});op(m,248,{lEntry});
    for(uint32_t v:{vT1,vT2,vT4,vT5,vMulti,vT6,vT7,vT8,vT9})op(m,59,{tPtrFnInt,v,7});
    op(m,59,{tPtrFnUint,vT3,7});op(m,59,{tPtrFnInt,vInit,7,c1});
    op(m,61,{tUint,50,inBone});op(m,124,{tInt,51,50});op(m,132,{tInt,52,51,c48});
    op(m,62,{vT1,52});op(m,61,{tInt,53,vT1});op(m,128,{tInt,54,53,c32});
    op(m,62,{vT2,54});op(m,61,{tInt,55,vT2});op(m,194,{tInt,56,55,c2});op(m,124,{tUint,57,56});
    op(m,62,{vT3,57});op(m,61,{tUint,58,vT3});op(m,124,{tInt,59,58});
    op(m,199,{tInt,60,59,c3});op(m,62,{vT4,60});                       // folds to 0
    op(m,128,{tInt,61,59,c1});op(m,199,{tInt,62,c3,61});op(m,62,{vT5,62});   // folds to 1 (mask on the left)
    op(m,132,{tInt,63,51,c20});op(m,194,{tInt,64,63,c2});op(m,199,{tInt,65,64,c3});op(m,62,{vT6,65});   // unknown
    op(m,62,{vMulti,52});op(m,62,{vMulti,54});op(m,61,{tInt,66,vMulti});op(m,194,{tInt,67,66,c2});op(m,199,{tInt,68,67,c3});op(m,62,{vT7,68});   // stored twice
    op(m,62,{vInit,54});op(m,61,{tInt,69,vInit});op(m,194,{tInt,70,69,c2});op(m,199,{tInt,71,70,c3});op(m,62,{vT8,71});   // initializer
    op(m,199,{tInt,72,59,c4});op(m,62,{vT9,72});                       // (word & 4): needs three bits, only two known
    op(m,253,{});op(m,56,{});
    return m;
}
// The value stored to each Function variable, for one input, by running the straight-line body.
std::map<uint32_t,uint32_t> run(const Module &m,uint32_t bone){
    std::map<uint32_t,uint32_t> val,mem,last;
    for(size_t i=5;i<m.size();){uint32_t wc=m[i]>>16,o=m[i]&0xffff;const uint32_t *a=&m[i+1];
        auto g=[&](uint32_t id){return val.count(id)?val[id]:0u;};
        if(o==43)val[a[1]]=a[2];
        else if(o==59 && a[2]==7 && wc>=5)mem[a[1]]=g(a[3]);
        else if(o==61)val[a[1]]=a[2]==inBone?bone:mem[a[2]];
        else if(o==62){mem[a[0]]=g(a[1]);last[a[0]]=g(a[1]);}
        else if(o==124)val[a[1]]=g(a[2]);
        else if(o==128)val[a[1]]=g(a[2])+g(a[3]);
        else if(o==132)val[a[1]]=g(a[2])*g(a[3]);
        else if(o==194)val[a[1]]=g(a[2])>>(g(a[3])&31);
        else if(o==199)val[a[1]]=g(a[2])&g(a[3]);
        i+=wc;}
    return last;
}
uint32_t stored_operand(const Module &m,uint32_t var){
    uint32_t r=0;for(size_t i=5;i<m.size();){uint32_t wc=m[i]>>16;if((m[i]&0xffff)==62 && m[i+1]==var)r=m[i+2];i+=wc;}return r;
}
uint32_t constant_value(const Module &m,uint32_t id){
    for(size_t i=5;i<m.size();){uint32_t wc=m[i]>>16;if((m[i]&0xffff)==43 && m[i+2]==id)return m[i+3];i+=wc;}
    throw std::runtime_error("not a constant");
}
void reject(const Module &in,const char *why){Module out{99};require(xbg_spirv_ldc_fold(in.data(),in.size(),out)==0 && out.empty(),why);}
}
int main(){try{
    Module in=module(),out;
    size_t k=xbg_spirv_ldc_fold(in.data(),in.size(),out);
    require(k==2,"expected exactly the two skinning components to fold");
    require(out.size()==in.size()+4 && out[3]==in[3]+1,"one new constant (0) and the bound raised by one");
    require(stored_operand(out,vT4)!=60 && constant_value(out,stored_operand(out,vT4))==0,"word & 3 must become the constant 0");
    require(stored_operand(out,vT5)==c1,"(word + 1) & 3 must reuse the existing constant 1");
    require(stored_operand(out,vT6)==65 && stored_operand(out,vT7)==68 && stored_operand(out,vT8)==71 && stored_operand(out,vT9)==72,"unprovable values stay");
    std::mt19937 rng(7);
    for(int t=0;t<20000;t++){uint32_t bone=rng();if(t<64)bone=uint32_t(t);
        auto a=run(in,bone),b=run(out,bone);
        for(uint32_t v:{vT4,vT5,vT6,vT7,vT8,vT9})require(a[v]==b[v],"a folded module stores a different value");}
    reject(Module{},"empty input");reject(Module{0x07230203},"short input");
    Module bad=module();bad[0]=0;reject(bad,"bad magic");
    bad=module();bad[5]=0;reject(bad,"zero word count");
    bad=module();bad[5]=0xffff0011;reject(bad,"word count past the end");
    Module none{0x07230203,0x00010300,0,8,0};op(none,17,{1});op(none,14,{0,1});reject(none,"nothing to fold");
    Module o2{99};require(!xbg_spirv_ldc_fold(nullptr,5,o2) && o2.empty(),"null input");
    std::cout<<"PASS ldc fold: 2 of 6 components folded (0 new, 1 reused), 4 kept (low bits short, stored twice, initializer, wide mask); 20000 inputs equal; rejects\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
