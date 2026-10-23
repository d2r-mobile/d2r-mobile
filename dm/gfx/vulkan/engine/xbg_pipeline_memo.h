// PR16前一draw管线memo的共享状态、生命周期与CPU可测逻辑；基于7f6ab77c并补代际与布局契约。
// Shared state, lifetime and CPU-testable previous-draw pipeline memo logic, based on PR16 7f6ab77c with generation/layout contracts.
#pragma once
#include "xbg_game_internal.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <type_traits>

inline uint64_t xbg_pipeline_next_generation(){
    static std::atomic<uint64_t> sequence{1};
    const uint64_t id=sequence.fetch_add(1,std::memory_order_relaxed);
    if(!id || id==UINT64_MAX)std::abort();
    return id;
}
// 构造、复制、移动及赋值都获得新代际；不能把临时对象的身份复制到新生命周期。
// Construction, copying, moving and assignment mint new generations instead of copying temporary-object identities.
struct XbgPipelineGeneration{
    uint64_t value=xbg_pipeline_next_generation();
    XbgPipelineGeneration()=default;
    XbgPipelineGeneration(const XbgPipelineGeneration&):value(xbg_pipeline_next_generation()){}
    XbgPipelineGeneration(XbgPipelineGeneration&&)noexcept:value(xbg_pipeline_next_generation()){}
    XbgPipelineGeneration& operator=(const XbgPipelineGeneration&){refresh();return *this;}
    XbgPipelineGeneration& operator=(XbgPipelineGeneration&&)noexcept{refresh();return *this;}
    void refresh(){value=xbg_pipeline_next_generation();}
};

// 与回放使用同一份真实State定义，CPU测试不会复制一个看似相同的镜像结构。
// Replay and CPU tests share the actual State definition rather than a lookalike test structure.
template<class ShaderType> struct XbgPipelineDrawState{
    p_set_rt rt{};ShaderType *shader[6]{};NVNbufferRange ub[6][16]{},sb[6][16]{},vb[16]{};
    uint64_t tex[6][128]{},samp[6][128]{},img[6][16]{};dpool_t texture_pool{},sampler_pool{};
    uint32_t attrib[16]{};vs_st stream[16]{};blend_st blend[8]{};color_st color{};chmask_st mask{0xffffffff};ms_st ms{};poly_st poly{0,1,2,0};ds_st ds{0,1,2,0,8,8,1,1,1,1,1,1};
    VkViewport viewport{0,0,1,1,0,1};VkRect2D scissor{{0,0},{UINT32_MAX,UINT32_MAX}};float blend_color[4]{},bias[3]{};uint32_t stencil_compare[2]{~0u,~0u},stencil_write[2]{~0u,~0u},stencil_ref[2]{};int64_t copy_stride=0,copy_image_stride=0;uint32_t patch_size=3;
    xbg_depth_bounds bounds{0,0,1};bool count_samples=false;uint64_t samples_passed=0;
};
constexpr size_t xbg_pipeline_align(size_t bytes,size_t alignment){return (bytes+alignment-1)/alignment*alignment;}

template<class S> struct XbgPipelineStateBytes{
    static_assert(std::is_standard_layout<S>::value && std::is_trivially_copyable<S>::value,"Pipeline state must have a byte-copyable standard layout");
    // 连续字段契约锁住切片起点和内部顺序；源码字段清单另捕获挤入ABI padding的新字段。
    // Consecutive-field contracts lock slice starts/order; the source inventory also catches new fields fitting ABI padding.
#define XBG_PIPE_NEXT(a,b) static_assert(offsetof(S,b)==xbg_pipeline_align(offsetof(S,a)+sizeof(S::a),alignof(decltype(S::b))),"Audit State field boundary " #a " -> " #b)
    static_assert(offsetof(S,rt)==0,"Audit State prefix");
    XBG_PIPE_NEXT(rt,shader);XBG_PIPE_NEXT(shader,ub);XBG_PIPE_NEXT(ub,sb);XBG_PIPE_NEXT(sb,vb);XBG_PIPE_NEXT(vb,tex);
    XBG_PIPE_NEXT(tex,samp);XBG_PIPE_NEXT(samp,img);XBG_PIPE_NEXT(img,texture_pool);XBG_PIPE_NEXT(texture_pool,sampler_pool);
    XBG_PIPE_NEXT(sampler_pool,attrib);XBG_PIPE_NEXT(attrib,stream);XBG_PIPE_NEXT(stream,blend);XBG_PIPE_NEXT(blend,color);
    XBG_PIPE_NEXT(color,mask);XBG_PIPE_NEXT(mask,ms);XBG_PIPE_NEXT(ms,poly);XBG_PIPE_NEXT(poly,ds);XBG_PIPE_NEXT(ds,viewport);
    XBG_PIPE_NEXT(viewport,scissor);XBG_PIPE_NEXT(scissor,blend_color);XBG_PIPE_NEXT(blend_color,bias);XBG_PIPE_NEXT(bias,stencil_compare);
    XBG_PIPE_NEXT(stencil_compare,stencil_write);XBG_PIPE_NEXT(stencil_write,stencil_ref);XBG_PIPE_NEXT(stencil_ref,copy_stride);
    XBG_PIPE_NEXT(copy_stride,copy_image_stride);XBG_PIPE_NEXT(copy_image_stride,patch_size);XBG_PIPE_NEXT(patch_size,bounds);
    XBG_PIPE_NEXT(bounds,count_samples);XBG_PIPE_NEXT(count_samples,samples_passed);
#undef XBG_PIPE_NEXT
    static_assert(offsetof(S,viewport)==xbg_pipeline_align(offsetof(S,ds)+sizeof(S::ds),alignof(VkViewport)),"Audit the attrib..ds slice endpoint");
    static_assert(offsetof(S,viewport)-offsetof(S,attrib)==xbg_pipeline_align(sizeof(S::attrib)+sizeof(S::stream)+sizeof(S::blend)+sizeof(S::color)+sizeof(S::mask)+sizeof(S::ms),alignof(poly_st))+sizeof(S::poly)+sizeof(S::ds),"Audit the complete fixed-state slice");
    static_assert(offsetof(S,copy_stride)==xbg_pipeline_align(offsetof(S,stencil_ref)+sizeof(S::stencil_ref),alignof(int64_t)),"Audit stencil slice endpoint");
    static_assert(offsetof(S,copy_stride)-offsetof(S,blend_color)==xbg_pipeline_align(sizeof(S::blend_color)+sizeof(S::bias)+sizeof(S::stencil_compare)+sizeof(S::stencil_write)+sizeof(S::stencil_ref),alignof(int64_t)),"Audit stencil slice length");
    static_assert(offsetof(S,count_samples)==offsetof(S,bounds)+sizeof(S::bounds),"Audit the patch/bounds slice endpoint");
    static_assert(offsetof(S,count_samples)-offsetof(S,patch_size)==sizeof(S::patch_size)+sizeof(S::bounds),"Audit the patch/bounds slice length");
    static_assert(sizeof(S)==xbg_pipeline_align(offsetof(S,samples_passed)+sizeof(S::samples_passed),alignof(S)),"New State tail fields require an explicit memo policy");
    std::array<uint8_t,offsetof(S,viewport)-offsetof(S,attrib)> fixed{};
    std::array<uint8_t,offsetof(S,copy_stride)-offsetof(S,blend_color)> stencil{};
    std::array<uint8_t,offsetof(S,count_samples)-offsetof(S,patch_size)> tail{};
    void fill(const S &s){const auto *b=reinterpret_cast<const uint8_t*>(&s);std::memcpy(fixed.data(),b+offsetof(S,attrib),fixed.size());std::memcpy(stencil.data(),b+offsetof(S,blend_color),stencil.size());std::memcpy(tail.data(),b+offsetof(S,patch_size),tail.size());}
};
template<class S> struct XbgPipelineMemoKey{
    const void *shader[5]{},*layout=nullptr,*pass_def=nullptr;
    uint64_t shader_generation[5]{},layout_generation=0,layout_definition_generation=0,pass_generation=0;
    VkRenderPass pass{};VkPipelineLayout pipeline_layout{};int32_t prim=0;uint32_t samples=0,rt_n=0,colors=0,depth=0,depth_format=0;
    XbgPipelineStateBytes<S> fields;
};
template<class S> void xbg_fill_pipeline_memo_key(XbgPipelineMemoKey<S> &k,const S &s,const void *layout,uint64_t layout_gen,uint64_t layout_def_gen,const void *pass_def,uint64_t pass_gen,VkRenderPass pass,VkPipelineLayout pipeline_layout,int prim,VkSampleCountFlagBits samples,uint32_t colors){
    std::memset(&k,0,sizeof k);
    for(unsigned i=0;i<5;i++){k.shader[i]=s.shader[i];k.shader_generation[i]=s.shader[i]?s.shader[i]->lifetime.value:0;}
    k.layout=layout;k.layout_generation=layout_gen;k.layout_definition_generation=layout_def_gen;k.pass_def=pass_def;k.pass_generation=pass_gen;
    k.pass=pass;k.pipeline_layout=pipeline_layout;k.prim=prim;k.samples=uint32_t(samples);k.rt_n=uint32_t(s.rt.n);k.colors=colors;k.depth=s.rt.depth!=nullptr;k.depth_format=s.rt.depth?uint32_t(s.rt.depth->d.format):0;
    k.fields.fill(s);
}
// 清理及淘汰统一调用invalidate；失效后不再读取保存的payload/entry指针。
// Cleanup and eviction call invalidate; invalid memos never read their saved payload/entry pointer.
template<class Key,class Payload> struct XbgPreviousPipelineMemo{
    static_assert(std::is_trivially_copyable<Key>::value,"Memo keys must be byte-copyable");
    Key key{};Payload payload{};bool valid=false;
    const Payload *find(const Key &candidate)const{return valid && std::memcmp(&key,&candidate,sizeof key)==0?&payload:nullptr;}
    void store(const Key &candidate,const Payload &value){key=candidate;payload=value;valid=true;}
    void invalidate(){valid=false;payload=Payload{};}
};
