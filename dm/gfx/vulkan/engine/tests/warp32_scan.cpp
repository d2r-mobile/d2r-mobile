// 全库离线调用生产warp改写器；按XGS1公开容器结构读取，不链接Vulkan、不修改源库。
// Offline library runner for the production warp rewriter, using the public XGS1 container layout without Vulkan or source-library writes.
#include "../xbg_spirv_warp.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc,char **argv){try{
    if(argc!=4){std::cerr<<"usage: warp32_scan <xgs-directory> <subgroup-size> <private-output-directory>\n";return 2;}
    unsigned subgroup=unsigned(std::stoul(argv[2]));std::filesystem::path input=argv[1],output=argv[3];
    if(!std::filesystem::is_directory(input))throw std::runtime_error("input is not a directory");
    if(std::filesystem::exists(output))throw std::runtime_error("output must be new, refusing to overwrite evidence");
    std::filesystem::create_directories(output);
    std::ofstream csv(output/"modules.csv");csv<<"file,stage,subgroup,sites,reason,input_words,output_words\n";
    std::vector<std::filesystem::path> files;for(auto &e:std::filesystem::directory_iterator(input))if(e.is_regular_file() && e.path().extension()==".xgs")files.push_back(e.path());
    std::sort(files.begin(),files.end());unsigned changed=0,compute=0,failed=0;size_t total_sites=0;
    for(auto &path:files){
        std::ifstream f(path,std::ios::binary|std::ios::ate);auto size=f.tellg();if(size<32 || size>64*1024*1024)throw std::runtime_error("invalid XGS file extent");
        std::vector<uint8_t> bytes(static_cast<size_t>(size));f.seekg(0);f.read(reinterpret_cast<char*>(bytes.data()),size);if(!f)throw std::runtime_error("short XGS read");
        uint32_t h[8];std::memcpy(h,bytes.data(),32);const uint64_t offset=32+uint64_t(h[3])*32+uint64_t(h[4])*4+h[5];
        if(h[0]!=0x31534758 || h[1]!=1 || h[2]>=6 || h[3]>256 || h[4]>16 || h[5]>65536 || h[6]%4 || offset+h[6]!=bytes.size())throw std::runtime_error("invalid XGS1 header/extent");
        std::vector<uint32_t> in(h[6]/4),out;std::memcpy(in.data(),bytes.data()+offset,h[6]);
        size_t sites=0;const char *reason="stage-not-compute";
        if(h[2]==5){++compute;sites=xbg_spirv_warp32(in.data(),in.size(),subgroup,out);
            reason=sites?"rewritten":subgroup!=64 && subgroup!=128?"unsupported-subgroup":"unchanged-or-conservatively-rejected";
            if(!sites && !out.empty())throw std::runtime_error("production reject returned nonempty output");}
        if(sites){++changed;total_sites+=sites;}else out=in;
        std::ofstream spv(output/(path.stem().string()+".spv"),std::ios::binary);spv.write(reinterpret_cast<const char*>(out.data()),std::streamsize(out.size()*4));if(!spv)throw std::runtime_error("cannot write module");
        csv<<path.filename().string()<<','<<h[2]<<','<<subgroup<<','<<sites<<','<<reason<<','<<in.size()<<','<<out.size()<<'\n';
    }
    if(!csv)throw std::runtime_error("cannot write CSV");
    std::cout<<"modules="<<files.size()<<" compute="<<compute<<" rewritten="<<changed<<" sites="<<total_sites<<" failures="<<failed<<" subgroup="<<subgroup<<"\n";
    std::cout<<"CPU production rewrite only; run spirv-val on every emitted module. Unchanged compute reasons remain deliberately coarse. No Vulkan driver execution.\n";
    return files.empty()?3:0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
