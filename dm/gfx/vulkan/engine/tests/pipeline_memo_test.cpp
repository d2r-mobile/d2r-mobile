// 直接测试生产memo helper和生产State模板；依据PR16的ABA、生命周期与固定输入契约。
// Test the production memo helper and State template against PR16 ABA, lifetime and fixed-input contracts.
#include "../xbg_pipeline_memo.h"
#include <iostream>
#include <new>
#include <stdexcept>
#include <utility>

namespace {
void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
struct Shader{XbgPipelineGeneration lifetime;};
using State=XbgPipelineDrawState<Shader>;
using Key=XbgPipelineMemoKey<State>;
using Memo=XbgPreviousPipelineMemo<Key,uint64_t>;
Key fill(const State &state,const XbgPipelineGeneration &layout,const XbgPipelineGeneration &definition,const XbgPipelineGeneration &pass,int prim=4,uint32_t colors=1,VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT){
    Key key;xbg_fill_pipeline_memo_key(key,state,&layout,layout.value,definition.value,&pass,pass.value,VK_NULL_HANDLE,VK_NULL_HANDLE,prim,samples,colors);return key;
}
bool same(const Key &a,const Key &b){return std::memcmp(&a,&b,sizeof a)==0;}
template<class F>void changed(const State &base,const Key &key,const XbgPipelineGeneration &layout,const XbgPipelineGeneration &definition,const XbgPipelineGeneration &pass,F mutation,const char *name){
    State state=base;mutation(state);require(!same(key,fill(state,layout,definition,pass)),name);
}
}
int main(){try{
    alignas(Shader) unsigned char storage[sizeof(Shader)];Shader *shader=new(storage)Shader;
    State state;state.shader[0]=shader;state.rt.n=1;
    XbgPipelineGeneration layout,definition,pass;Key key=fill(state,layout,definition,pass);Memo memo;
    require(!memo.find(key),"empty memo hit");memo.store(key,17);require(memo.find(key) && *memo.find(key)==17,"equal production key missed");
    uint64_t old=shader->lifetime.value;shader->~Shader();shader=new(storage)Shader;require(shader->lifetime.value!=old,"shader address reuse copied generation");
    require(!memo.find(fill(state,layout,definition,pass)),"shader ABA hit");
    key=fill(state,layout,definition,pass);memo.store(key,18);
    old=layout.value;layout.~XbgPipelineGeneration();new(&layout)XbgPipelineGeneration;require(layout.value!=old && !memo.find(fill(state,layout,definition,pass)),"layout ABA hit");
    key=fill(state,layout,definition,pass);memo.store(key,19);
    old=pass.value;pass.~XbgPipelineGeneration();new(&pass)XbgPipelineGeneration;require(pass.value!=old && !memo.find(fill(state,layout,definition,pass)),"render definition ABA hit");
    key=fill(state,layout,definition,pass);memo.store(key,20);definition.refresh();require(!memo.find(fill(state,layout,definition,pass)),"layout definition mutation hit");
    XbgPipelineGeneration copied=layout,moved=std::move(copied);require(layout.value!=copied.value && moved.value!=copied.value,"generation copied on copy/move construction");
    old=moved.value;moved=layout;require(moved.value!=old && moved.value!=layout.value,"generation copied on assignment");
    key=fill(state,layout,definition,pass);memo.store(key,21);memo.invalidate();require(!memo.find(key) && memo.payload==0,"destroy/evict/clear retained payload");
    memo.store(key,22);memo.invalidate();memo.store(key,23);require(memo.find(key) && *memo.find(key)==23,"cache failed after invalidation and refill");
    // 修改真实State的各切片字段；测试直接调用生产fill，未重写一份key算法。
    // Mutate each actual State slice field, calling production fill instead of mirroring its key algorithm.
#define CHECK(field) changed(state,key,layout,definition,pass,[](State &s){reinterpret_cast<unsigned char*>(&s.field)[0]^=1;},"missing field " #field)
    CHECK(attrib);CHECK(stream);CHECK(blend);CHECK(color);CHECK(mask);CHECK(ms);CHECK(poly);CHECK(ds);
    CHECK(blend_color);CHECK(bias);CHECK(stencil_compare);CHECK(stencil_write);CHECK(stencil_ref);CHECK(patch_size);CHECK(bounds);
#undef CHECK
    changed(state,key,layout,definition,pass,[](State &s){s.rt.n=2;},"target count missing");
    tex_t depth{};depth.d.format=0x32;state.rt.depth=&depth;Key depth_key=fill(state,layout,definition,pass);
    require(!same(key,depth_key),"depth attachment presence missing");depth.d.format=0x34;require(!same(depth_key,fill(state,layout,definition,pass)),"depth format missing");state.rt.depth=nullptr;
    require(!same(key,fill(state,layout,definition,pass,5)),"primitive missing");
    require(!same(key,fill(state,layout,definition,pass,4,3)),"active colors missing");
    require(!same(key,fill(state,layout,definition,pass,4,1,VK_SAMPLE_COUNT_4_BIT)),"sample count missing");
    State dynamic=state;dynamic.viewport.width=99;dynamic.scissor.extent.width=11;dynamic.copy_stride=1024;dynamic.copy_image_stride=4096;dynamic.samples_passed=17;dynamic.count_samples=true;
    require(same(key,fill(dynamic,layout,definition,pass)),"non-pipeline dynamic/copy/query inputs incorrectly changed the key");
    dynamic.ub[0][0].address=123;dynamic.vb[0].address=456;dynamic.tex[0][0]=7;dynamic.samp[0][0]=9;dynamic.img[0][0]=11;
    require(same(key,fill(dynamic,layout,definition,pass)),"resource addresses incorrectly changed the pipeline key");
    shader->~Shader();
    std::cout<<"PASS production memo: same-address shader/layout/definition generations, mutation/copy/move, invalidation/refill, 15 State slice fields, targets/format/primitive/colors/samples, dynamic/resource exclusions; CPU only\n";return 0;
}catch(const std::exception &e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}

#ifdef XBG_PIPELINE_MEMO_NEGATIVE_STATE
// 编译负控制：新增未分类尾字段必须触发布局契约失败，root应确认该构建失败。
// Negative compile control: an unclassified new tail field must fail the layout contract; root should require compilation to fail.
struct MissingPolicyState:State{uint32_t unclassified_pipeline_field;};
XbgPipelineStateBytes<MissingPolicyState> must_fail_layout_contract;
#endif
