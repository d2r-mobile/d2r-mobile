// 直接调用生产的光栅事实检测，喂合成 SPIR-V：翻译器的写法（先写常量、再逐分量抄输入）认得出，任何一处不是原样抄就不认。
// Call the production raster-facts detection on synthetic SPIR-V: the translator's pattern (constants first, then each
// component copied from an input) is recognized, and anything that is not a plain copy is not.
#include "../xbg_spirv_raster.h"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Module=std::vector<uint32_t>;
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void op(Module &m,uint32_t opcode,std::initializer_list<uint32_t> args){m.push_back((uint32_t(args.size()+1)<<16)|opcode);m.insert(m.end(),args);}
enum : uint32_t {tVoid=1,tFn=2,tFloat=3,tVec4=4,tInt=5,tBlock=6,tPtrInV4=7,tPtrOutF=8,tPtrOutBlock=9,tPtrOutV4=10,tPtrInF=11,
    i0=20,i1=21,i2=22,i3=23,f0=24,f1=25,inA=30,inB=31,outPV=32,outC0=33,outC2=34,outMask=35,fMain=40,lEntry=41,lNext=42};
enum Variant {Plain,WFromInput,Swizzled,ClipWritten,TwoBlocks,WholeVector,OtherInput};
// Vertex module: gl_PerVertex {vec4 Position; float PointSize; float ClipDistance}, inputs at locations 0 (inA) and 3 (inB).
Module vertex(Variant v){
    Module m{0x07230203,0x00010300,0,100,0};
    op(m,17,{1});op(m,14,{0,1});
    op(m,15,{0,fMain,0x6e69616d,0,inA,inB,outPV});
    op(m,71,{inA,30,0});op(m,71,{inB,30,3});
    op(m,71,{tBlock,2});op(m,72,{tBlock,0,11,0});op(m,72,{tBlock,1,11,1});op(m,72,{tBlock,2,11,3});
    op(m,19,{tVoid});op(m,33,{tFn,tVoid});op(m,22,{tFloat,32});op(m,23,{tVec4,tFloat,4});op(m,21,{tInt,32,1});
    op(m,30,{tBlock,tVec4,tFloat,tFloat});
    op(m,32,{tPtrInV4,1,tVec4});op(m,32,{tPtrOutF,3,tFloat});op(m,32,{tPtrOutBlock,3,tBlock});op(m,32,{tPtrOutV4,3,tVec4});op(m,32,{tPtrInF,1,tFloat});
    op(m,43,{tInt,i0,0});op(m,43,{tInt,i1,1});op(m,43,{tInt,i2,2});op(m,43,{tInt,i3,3});op(m,43,{tFloat,f0,0});op(m,43,{tFloat,f1,0x3f800000});
    op(m,59,{tPtrInV4,inA,1});op(m,59,{tPtrInV4,inB,1});op(m,59,{tPtrOutBlock,outPV,3});
    op(m,54,{tVoid,fMain,0,tFn});op(m,248,{lEntry});
    uint32_t id=50;
    auto store_pos=[&](uint32_t comp,uint32_t value){uint32_t p=id++;op(m,65,{tPtrOutF,p,outPV,i0,comp});op(m,62,{p,value});};
    auto load_in=[&](uint32_t var,uint32_t comp){uint32_t p=id++,r=id++;op(m,65,{tPtrInF,p,var,comp});op(m,61,{tFloat,r,p});return r;};
    // the translator's start: (0, 0, 0, 1)
    store_pos(i0,f0);store_pos(i1,f0);store_pos(i2,f0);store_pos(i3,f1);
    if(v==WholeVector){uint32_t r=id++;op(m,61,{tVec4,r,inA});uint32_t p=id++;op(m,65,{tPtrOutV4,p,outPV,i0});op(m,62,{p,r});}
    else{
        uint32_t x=load_in(inA,i0),y=load_in(inA,i1),z=load_in(v==Swizzled?inA:v==OtherInput?inB:inA,v==Swizzled?i3:i2);
        uint32_t cx=id++;op(m,83,{tFloat,cx,x});
        store_pos(i0,cx);store_pos(i1,y);store_pos(i2,z);
        if(v==WFromInput)store_pos(i3,load_in(inA,i3));
        if(v==ClipWritten){uint32_t p=id++;op(m,65,{tPtrOutF,p,outPV,i2});op(m,62,{p,f0});}
    }
    if(v==TwoBlocks){op(m,249,{lNext});op(m,248,{lNext});}
    op(m,253,{});op(m,56,{});
    m[3]=id;return m;
}
Module fragment(){
    Module m{0x07230203,0x00010300,0,100,0};
    op(m,17,{1});op(m,14,{0,1});
    op(m,15,{4,fMain,0x6e69616d,0,outC0,outC2,outMask});
    op(m,71,{outC0,30,0});op(m,71,{outC2,30,2});op(m,71,{outMask,11,20});
    op(m,19,{tVoid});op(m,33,{tFn,tVoid});op(m,22,{tFloat,32});op(m,23,{tVec4,tFloat,4});op(m,21,{tInt,32,1});
    op(m,32,{tPtrOutV4,3,tVec4});op(m,32,{tPtrOutF,3,tInt});
    op(m,59,{tPtrOutV4,outC0,3});op(m,59,{tPtrOutV4,outC2,3});op(m,59,{tPtrOutF,outMask,3});
    op(m,54,{tVoid,fMain,0,tFn});op(m,248,{lEntry});op(m,253,{});op(m,56,{});
    return m;
}
}

int main(){
    try{
        auto facts=[](Variant v){Module m=vertex(v);return xbg_spirv_raster_facts(m.data(),m.size(),0);};
        XbgRasterFacts a=facts(Plain);require(a.pos_input==0 && a.pos_w1,"plain copy of input 0 with w = 1");
        XbgRasterFacts b=facts(WFromInput);require(b.pos_input==0 && !b.pos_w1,"w copied from the same input");
        require(facts(Swizzled).pos_input==-1,"z taken from component 3");
        require(facts(OtherInput).pos_input==-1,"z taken from another input");
        require(facts(ClipWritten).pos_input==-1,"ClipDistance written");
        require(facts(TwoBlocks).pos_input==-1,"more than one block");
        XbgRasterFacts w=facts(WholeVector);require(w.pos_input==0 && !w.pos_w1,"whole input vector stored, w from the input");
        {Module m=vertex(Plain);require(xbg_spirv_raster_facts(m.data(),m.size(),4).pos_input==-1,"vertex module read as fragment");}
        {Module m=fragment();XbgRasterFacts f=xbg_spirv_raster_facts(m.data(),m.size(),4);require(f.outputs==0x5u && f.sample_mask,"fragment outputs 0 and 2, SampleMask");}
        {Module m=vertex(Plain);m[0]=0;require(xbg_spirv_raster_facts(m.data(),m.size(),0).pos_input==-1,"bad magic");}
    }catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
    std::cout<<"PASS raster facts\n";return 0;
}
