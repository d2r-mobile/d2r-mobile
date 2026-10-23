// 直接调用生产的单写临时变量转发并检查合成 SPIR-V：该删的删、该留的留、引用全部有定义，输出跑出与原来相同的值。
// Call the production single-store temporary forwarding on synthetic SPIR-V: what may go goes, what must stay stays, every
// referenced id is defined, and the output computes the same values as the input.
#include "../xbg_spirv_temps.h"
#include <cstdint>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
using Module=std::vector<uint32_t>;
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void op(Module &m,uint32_t opcode,std::initializer_list<uint32_t> args){m.push_back((uint32_t(args.size()+1)<<16)|opcode);m.insert(m.end(),args);}
enum : uint32_t {tVoid=1,tFn=2,tInt=4,tPtrIn=5,tPtrOut=6,tPtrFn=7,tBool=8,
    c1=10,c2=11,c7=12,inX=30,outA=31,outB=32,outC=33,outD=34,outE=35,outF=36,outG=37,fMain=20,lEntry=21,lNext=22,
    vFwd=40,vChain=41,vTwice=42,vInit=43,vOther=44,vNamed=45,vDead=46,vNever=47,vArr=48};
// One function, two blocks. Forwardable: vFwd, vChain (stores vFwd's load), vDead (stored, never loaded). Must stay: vOther
// (loaded in the next block), vTwice (stored twice), vInit (initializer), vNamed (OpName), vNever (loaded, never stored),
// vArr (written by an OpCopyMemory, which the pass does not follow).
Module module(){
    Module m{0x07230203,0x00010300,0,100,0};
    op(m,17,{1});op(m,14,{0,1});
    op(m,15,{0,fMain,0x6e69616d,0,inX,outA,outB,outC,outD,outE,outF,outG});
    op(m,5,{vNamed,0x00706d74});                                            // OpName vNamed "tmp"
    for(uint32_t v:{inX})op(m,71,{v,30,0});
    {uint32_t l=0;for(uint32_t v:{outA,outB,outC,outD,outE,outF,outG})op(m,71,{v,30,l++});}
    op(m,19,{tVoid});op(m,33,{tFn,tVoid});op(m,21,{tInt,32,1});op(m,20,{tBool});
    op(m,32,{tPtrIn,1,tInt});op(m,32,{tPtrOut,3,tInt});op(m,32,{tPtrFn,7,tInt});
    op(m,43,{tInt,c1,1});op(m,43,{tInt,c2,2});op(m,43,{tInt,c7,7});
    op(m,59,{tPtrIn,inX,1});for(uint32_t v:{outA,outB,outC,outD,outE,outF,outG})op(m,59,{tPtrOut,v,3});
    op(m,54,{tVoid,fMain,0,tFn});op(m,248,{lEntry});
    for(uint32_t v:{vFwd,vChain,vTwice,vOther,vNamed,vDead,vNever,vArr})op(m,59,{tPtrFn,v,7});
    op(m,59,{tPtrFn,vInit,7,c7});
    op(m,61,{tInt,50,inX});op(m,128,{tInt,51,50,c1});
    op(m,62,{vFwd,51});op(m,61,{tInt,52,vFwd});op(m,61,{tInt,53,vFwd});op(m,128,{tInt,54,52,53});op(m,62,{outA,54});
    op(m,62,{vChain,52});op(m,61,{tInt,55,vChain});op(m,62,{outB,55});
    op(m,62,{vTwice,51});op(m,61,{tInt,56,vTwice});op(m,62,{vTwice,c2});op(m,61,{tInt,57,vTwice});op(m,128,{tInt,58,56,57});op(m,62,{outC,58});
    op(m,61,{tInt,59,vInit});op(m,62,{vInit,51});op(m,61,{tInt,60,vInit});op(m,128,{tInt,61,59,60});op(m,62,{outD,61});
    op(m,62,{vOther,51});op(m,62,{vNamed,51});op(m,61,{tInt,62,vNamed});op(m,62,{outE,62});
    op(m,62,{vDead,51});op(m,61,{tInt,63,vNever});op(m,62,{vArr,51});op(m,63,{vArr,vTwice});
    op(m,249,{lNext});op(m,248,{lNext});
    op(m,61,{tInt,64,vOther});op(m,62,{outF,64});op(m,61,{tInt,65,vArr});op(m,62,{outG,65});
    op(m,253,{});op(m,56,{});
    return m;
}
std::set<uint32_t> defined(const Module &m){
    std::set<uint32_t> d;
    for(size_t i=5;i<m.size();){uint32_t wc=m[i]>>16,o=m[i]&0xffff;
        if(o==19||o==20||o==21||o==32||o==33||o==248)d.insert(m[i+1]);
        else if(o==43||o==59||o==61||o==128||o==54)d.insert(m[i+2]);
        i+=wc;}
    return d;
}
size_t count(const Module &m,uint32_t opcode,uint32_t word1){size_t c=0;for(size_t i=5;i<m.size();){uint32_t wc=m[i]>>16;if((m[i]&0xffff)==opcode && m[i+1]==word1)c++;i+=wc;}return c;}
// The value written to each output, by running the two straight-line blocks (OpCopyMemory copies the source variable).
std::map<uint32_t,uint32_t> run(const Module &m,uint32_t x){
    std::map<uint32_t,uint32_t> val,mem;
    for(size_t i=5;i<m.size();){uint32_t wc=m[i]>>16,o=m[i]&0xffff;const uint32_t *a=&m[i+1];
        auto g=[&](uint32_t id){return val.count(id)?val[id]:0u;};
        if(o==43)val[a[1]]=a[2];
        else if(o==59 && a[2]==7 && wc>=5)mem[a[1]]=g(a[3]);
        else if(o==61)mem.count(a[2]) || a[2]==inX?void(val[a[1]]=a[2]==inX?x:mem[a[2]]):void(val[a[1]]=0);
        else if(o==62)mem[a[0]]=g(a[1]);
        else if(o==63)mem[a[0]]=mem[a[1]];
        else if(o==128)val[a[1]]=g(a[2])+g(a[3]);
        i+=wc;}
    std::map<uint32_t,uint32_t> r;for(uint32_t v:{outA,outB,outC,outD,outE,outF,outG})r[v]=mem[v];return r;
}
void reject(const Module &in,const char *why){Module out{99};require(xbg_spirv_forward_temps(in.data(),in.size(),out)==0 && out.empty(),why);}
}
int main(){try{
    Module in=module(),out;
    size_t k=xbg_spirv_forward_temps(in.data(),in.size(),out);
    require(k==3,"expected vFwd, vChain and vDead to go");
    for(uint32_t v:{vFwd,vChain,vDead})require(!count(out,62,v) && !defined(out).count(v),"a forwarded variable or its store is left");
    for(uint32_t v:{vTwice,vInit,vOther,vNamed,vNever,vArr})require(defined(out).count(v),"a variable that must stay was removed");
    require(count(out,62,outB)==1,"the chained store must remain");
    auto d=defined(out);
    for(size_t i=5;i<out.size();){uint32_t wc=out[i]>>16,o=out[i]&0xffff;
        if(o==62||o==128||o==61||o==63)for(uint32_t w=(o==128||o==61)?3:1;w<wc && w<=4;w++)require(d.count(out[i+w]),"an operand refers to a removed id");
        i+=wc;}
    std::mt19937 rng(11);
    for(int t=0;t<2000;t++){uint32_t x=rng();require(run(in,x)==run(out,x),"the forwarded module writes different outputs");}
    reject(Module{},"empty input");reject(Module{0x07230203},"short input");
    Module bad=module();bad[0]=0;reject(bad,"bad magic");bad=module();bad[5]=0;reject(bad,"zero word count");
    bad=module();bad[5]=0xffff0011;reject(bad,"word count past the end");
    Module none{0x07230203,0x00010300,0,8,0};op(none,17,{1});op(none,14,{0,1});reject(none,"no function");
    Module o2{99};require(!xbg_spirv_forward_temps(nullptr,5,o2) && o2.empty(),"null input");
    std::cout<<"PASS forward temps: 3 of 9 variables removed (forwarded, chained, dead), 6 kept (other block, stored twice, initializer, named, never stored, copied); outputs equal over 2000 inputs; rejects\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
