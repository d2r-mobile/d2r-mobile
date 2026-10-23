// 直接调用生产warp改写器并解释其输出的合成SPIR-V；依据PR21的32道分组契约。
// Call the production warp rewriter and interpret its synthetic SPIR-V output against the PR21 32-lane partition contract.
#include "../xbg_spirv_warp.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Module=std::vector<uint32_t>;
void require(bool ok,const char *why){if(!ok)throw std::runtime_error(why);}
void op(Module &m,uint32_t opcode,std::initializer_list<uint32_t> args){m.push_back((uint32_t(args.size()+1)<<16)|opcode);m.insert(m.end(),args);}
enum Fixture{Full,SignedLane,NoLane,SecondEntry,MaskWordOne,MaskCopy,Geometry,Unsupported,Fragment};
Module module(Fixture kind){
    Module m{0x07230203,0x00010300,0,64,0};
    for(uint32_t cap:{1u,61u,62u,63u,64u,65u})op(m,17,{cap});
    op(m,14,{0,1});
    const bool lane=kind!=NoLane && kind!=SecondEntry;
    const uint32_t model=kind==Fragment?4:5;
    if(lane)op(m,15,{model,20,0x6e69616d,0,18,19,22});else op(m,15,{model,20,0x6e69616d,0,19,22});
    if(kind==SecondEntry)op(m,15,{5,20,0x00746c61,19,22});
    op(m,16,{20,17,128,1,1});
    if(lane)op(m,71,{18,11,41});
    op(m,71,{19,11,4420});op(m,71,{22,11,28});
    if(kind==Geometry)op(m,71,{23,11,36});
    op(m,19,{1});op(m,33,{2,1});op(m,20,{3});op(m,21,{4,32,0});op(m,23,{5,4,4});op(m,23,{6,4,3});
    op(m,32,{7,1,4});op(m,32,{8,1,5});op(m,32,{9,1,6});op(m,21,{10,32,1});op(m,32,{11,1,10});
    op(m,43,{4,12,0});op(m,43,{4,13,3});op(m,43,{4,14,7});op(m,43,{4,15,32});op(m,43,{4,16,1032});
    if(lane)op(m,59,{kind==SignedLane?11u:7u,18,1});
    op(m,59,{8,19,1});op(m,59,{9,22,1});if(kind==Geometry)op(m,59,{7,23,1});
    op(m,54,{1,20,0,2});op(m,248,{21});
    if(lane)op(m,61,{kind==SignedLane?10u:4u,30,18});
    op(m,61,{6,31,22});op(m,81,{4,32,31,0});op(m,176,{3,33,32,16});
    op(m,339,{5,34,13,33});op(m,81,{4,35,34,0});
    op(m,61,{5,36,19});op(m,81,{4,37,36,kind==MaskWordOne?1u:0u});
    op(m,334,{3,38,13,33});op(m,335,{3,39,13,33});op(m,345,{4,40,13,32,14});
    op(m,65,{7,41,19,12});op(m,61,{4,42,41});
    if(kind==MaskCopy)op(m,83,{5,44,36});
    if(kind==Unsupported)op(m,349,{4,45,13,0,32});
    op(m,253,{});op(m,56,{});return m;
}
struct Value{std::array<uint32_t,4> v{};};
using Lanes=std::vector<Value>;
struct Evaluation{std::vector<Lanes> values;};
Evaluation evaluate(const Module &m,unsigned width,const std::vector<bool> &active){
    Evaluation e;e.values.assign(m[3],Lanes(width));std::vector<uint32_t> builtin(m[3]),type(m[3]);
    for(size_t at=5;at<m.size();){unsigned wc=m[at]>>16,opcode=m[at]&65535;require(wc && at+wc<=m.size(),"bad emitted instruction framing");const uint32_t *p=m.data()+at;
        if(opcode==71 && wc>=4 && p[2]==11)builtin[p[1]]=p[3];
        if(opcode==21)type[p[1]]=p[3];
        if(opcode==43)for(unsigned l=0;l<width;l++)e.values[p[2]][l].v[0]=p[3];
        if(opcode==41)for(unsigned l=0;l<width;l++)e.values[p[2]][l].v[0]=1;
        if(opcode==61){for(unsigned l=0;l<width;l++){
            auto &v=e.values[p[2]][l].v;
            if(builtin[p[3]]==41)v[0]=l;
            else if(builtin[p[3]]==28)v[0]=1000+l;
            else if(builtin[p[3]]==4420)for(unsigned k=0;k<4;k++)v[k]=l>=32*(k+1)?0xffffffffu:l<=32*k?0u:(uint32_t(1)<<(l-32*k))-1;
            else throw std::runtime_error("unhandled production load in evaluator");
        }}
        if(opcode==81)for(unsigned l=0;l<width;l++)e.values[p[2]][l].v[0]=e.values[p[3]][l].v[p[4]];
        if(opcode==124)for(unsigned l=0;l<width;l++)e.values[p[2]][l]=e.values[p[3]][l];
        if(opcode==169 || opcode==170 || opcode==171 || opcode==176 || opcode==194 || opcode==197 || opcode==199){
            for(unsigned l=0;l<width;l++){
                uint32_t a=e.values[p[3]][l].v[0],b=e.values[p[4]][l].v[0],r=0;
                if(opcode==169)r=e.values[p[a?4:5]][l].v[0];
                if(opcode==170)r=a==b;if(opcode==171)r=a!=b;if(opcode==176)r=a<b;
                if(opcode==194)r=a>>b;if(opcode==197)r=a|b;if(opcode==199)r=a&b;
                e.values[p[2]][l].v[0]=r;
            }
        }
        if(opcode==339){Value ballot;for(unsigned l=0;l<width;l++)if(active[l] && e.values[p[4]][l].v[0])ballot.v[l/32]|=uint32_t(1)<<(l%32);
            for(unsigned l=0;l<width;l++)e.values[p[2]][l]=ballot;}
        if(opcode==345)for(unsigned l=0;l<width;l++){
            unsigned src=e.values[p[5]][l].v[0];require(src<width,"shuffle source exceeds subgroup");e.values[p[2]][l]=e.values[p[4]][src];
        }
        at+=wc;
    }
    return e;
}
void check_types(const Module &m){
    std::vector<uint32_t> t(m[3]),component(m[3]);
    for(size_t at=5;at<m.size();){auto p=m.data()+at;unsigned wc=p[0]>>16,code=p[0]&65535;
        require(wc && at+wc<=m.size(),"bad output framing");
        if(code==23)component[p[1]]=p[2];
        if(code==41 || code==43 || code==59 || code==61 || code==65 || code==81 || code==124 || code==169 || code==170 || code==171 || code==176 || code==194 || code==197 || code==199 || code==339 || code==345)t[p[2]]=p[1];
        if(code==81)require(t[p[2]]==component[t[p[3]]],"extract component/result type mismatch");
        if(code==199 || code==197)require(t[p[2]]==t[p[3]] && t[p[2]]==t[p[4]],"bitwise operand/result type mismatch");
        if(code==169)require(t[p[2]]==t[p[4]] && t[p[2]]==t[p[5]],"select operand/result type mismatch");
        require(code!=77,"dynamic vector extraction reintroduced");
        at+=wc;
    }
}
void dump(const std::filesystem::path &dir,const std::string &name,const Module &m){if(dir.empty())return;std::filesystem::create_directories(dir);std::ofstream f(dir/(name+".spv"),std::ios::binary);f.write(reinterpret_cast<const char*>(m.data()),std::streamsize(m.size()*4));require(bool(f),"cannot write fixture");}
void run_case(Fixture fixture,unsigned width,const std::filesystem::path &dir){
    Module in=module(fixture),out;size_t count=xbg_spirv_warp32(in.data(),in.size(),width,out);require(count>0 && !out.empty(),"expected a production rewrite");check_types(out);
    const std::string name=std::to_string(unsigned(fixture))+"-"+std::to_string(width);dump(dir,name+"-input",in);dump(dir,name+"-rewritten",out);
    for(bool partial:{false,true}){
        std::vector<bool> active(width,true);if(partial)for(unsigned l=0;l<width;l++)active[l]=(l%32)<20;
        auto e=evaluate(out,width,active);
        for(unsigned l=0;l<width;l++)if(active[l]){
            if(fixture!=NoLane)require(e.values[30][l].v[0]==l%32,"lane&31 mismatch");
            uint32_t expected=0;for(unsigned x=(l/32)*32;x<(l/32+1)*32;x++)if(active[x] && x<32)expected|=uint32_t(1)<<(x%32);
            require(e.values[35][l].v[0]==expected,"ballot crossed a 32-lane partition");
            const uint32_t lower=l%32?((uint32_t(1)<<(l%32))-1):0;
            require(e.values[37][l].v[0]==lower && e.values[42][l].v[0]==lower,"mask extract/chain used the wrong word");
            require(e.values[38][l].v[0]==(l<32) && e.values[39][l].v[0]==(l<32),"all/any crossed a 32-lane partition");
            require(e.values[40][l].v[0]==1000+(l&96)+7,"shuffle crossed a 32-lane partition");
        }
    }
    std::cout<<"rewrite fixture="<<unsigned(fixture)<<" subgroup="<<width<<" sites="<<count<<" words="<<out.size()<<'\n';
}
void reject(Module in,unsigned width){Module out{99};require(xbg_spirv_warp32(in.data(),in.size(),width,out)==0 && out.empty(),"reject must return zero and clear output");}
}
int main(int argc,char **argv){try{
    std::filesystem::path dir;if(argc==3 && std::string(argv[1])=="--dump-dir")dir=argv[2];else require(argc==1,"usage: warp32_test [--dump-dir path]");
    for(unsigned width:{64u,128u})for(auto f:{Full,SignedLane,NoLane})run_case(f,width,dir);
    for(unsigned width:{0u,1u,16u,32u,33u,48u,63u,65u,96u,127u,129u,256u})reject(module(Full),width);
    for(auto f:{SecondEntry,MaskWordOne,MaskCopy,Geometry,Unsupported,Fragment})for(unsigned width:{64u,128u})reject(module(f),width);
    Module short_input{0x07230203};reject(short_input,64);Module bad=module(Full);bad[0]=0;reject(bad,64);
    bad=module(Full);bad[5]=0;reject(bad,64);bad=module(Full);bad[5]=0xffff0011;reject(bad,64);
    Module out{99};require(!xbg_spirv_warp32(nullptr,5,64,out) && out.empty(),"null input reject");
    std::cout<<"PASS production warp32: 64/128 four partitions, ballot/mask/chain/all/any/shuffle, signed lane types, active masks, 32 and unsupported/reject cases; CPU only, no Vulkan driver\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
