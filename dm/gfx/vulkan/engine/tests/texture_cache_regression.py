"""Run actual CPU-only registration/hash snippets as WASM; no Vulkan/device calls.

Usage: python texture_cache_regression.py --compiler <clang++.exe> [--node node]
The extracted snippets deliberately keep the production field comparison and
hash cadence. This is CPU evidence, not a Vulkan/GPU rendering test.
"""
import argparse
import pathlib
import re
import subprocess
import tempfile


def balanced(source, start):
    end = source.index('{', start)
    depth = 1
    end += 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', required=True)
    parser.add_argument('--node', default='node')
    args = parser.parse_args()
    engine = pathlib.Path(__file__).resolve().parent.parent
    source = (engine / 'xbg_game.cpp').read_text(encoding='utf-8')
    nvn = engine.parent.parent / 'nvn'
    types = '\n'.join((nvn / p).read_text(encoding='utf-8')
                      for p in ('nvn_tex.h', 'nvn_obj.h'))
    declarations = []
    for name in ('nvn_texdesc', 'nvn_texlayout', 'tex_t', 'view_t'):
        declarations.append(re.search(r'typedef struct \{[^{}]*\}\s*' + name + r';', types)[0])
    registration = source[source.index('struct RegisteredTexture{'):
                          source.index('struct MemoImage{')]
    cadence = balanced(source, source.index('if(upload && im.uploaded && !im.gpu_written'))
    # Integration checks complement the executed CPU tests below; they are not GPU tests.
    assert 'cached.record->revision!=cached.revision' in source
    assert 'it->second.record->revision==it->second.revision' in source
    assert 'for(auto &ti:m.images){Image &im=get_image(ti.first,true);' in source
    assert 'Image *im=&get_image(it->second.tex,true);' in source
    guard = re.search(r'^\s*if\(b.type==VK_DESCRIPTOR_TYPE_STORAGE_IMAGE\)\{ok=false;\+\+desc_memo_why\[1\];break;\}',
                      source, re.MULTILINE)[0]
    harness = r'''
typedef __UINT8_TYPE__ uint8_t;
typedef __UINT32_TYPE__ uint32_t;
typedef __UINT64_TYPE__ uint64_t;
typedef __INT64_TYPE__ int64_t;
typedef __INT32_TYPE__ int32_t;
typedef __SIZE_TYPE__ size_t;
struct pool_t {};
extern "C" void *memcpy(void *dst,const void *src,size_t n){
    for(size_t i=0;i<n;i++)((uint8_t*)dst)[i]=((const uint8_t*)src)[i];return dst;
}
extern "C" void *memset(void *dst,int value,size_t n){
    for(size_t i=0;i<n;i++)((uint8_t*)dst)[i]=uint8_t(value);return dst;
}
int memcmp(const void *a,const void *b,size_t n){
    const uint8_t *x=(const uint8_t*)a,*y=(const uint8_t*)b;
    for(size_t i=0;i<n;i++)if(x[i]!=y[i])return x[i]<y[i]?-1:1;return 0;
}
'''
    harness += '\n'.join(declarations) + '\n' + registration
    harness += r'''
struct CpuImage {bool uploaded=true,gpu_written=false;uint64_t checked_submission=~uint64_t(0),cpu_hash=0,dirty_stamp=0,dirty_seen=0;uint32_t stable_checks=0;};
// No dirty-page tracker (gfx-tex-dirty off, hooks absent), so the extracted cadence runs its hash checks.
bool tex_dirty=false;uint64_t tex_dirty_tracked=0,tex_dirty_checks=0,tex_dirty_changed=0;
bool ab_off(const char*){return false;}
int (*nvn_dirty_tracked)(const void*,size_t)=nullptr;void (*nvn_dirty_protect)(const void*,size_t)=nullptr;
uint64_t (*nvn_dirty_now)()=nullptr;int (*nvn_dirty_since)(const void*,size_t,uint64_t)=nullptr;
struct {uint64_t submissions=0;} stats;
unsigned hash_calls=0;
uint64_t hash_memory(const void *memory,size_t n){++hash_calls;uint64_t h=0;for(size_t i=0;i<n;i++)h=h*31+((const uint8_t*)memory)[i];return h;}
void check_cpu(CpuImage &im,const tex_t *t,bool upload){
'''+cadence+r'''
}
enum {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE=3};
bool memo_eligible(int type){struct {int type;} b{type};bool ok=true;unsigned desc_memo_why[6]={};do {
'''+guard+r'''
}while(false);return ok;}
#define CHECK(x) do{if(!(x))return __LINE__;}while(false)
extern "C" int run_tests(){
    uint8_t pixels[4]={1,2,3,4};pool_t pool;
    tex_t t{};t.serial=7;t.mem=pixels;t.pool=&pool;t.L.size=4;t.d.width=t.d.height=t.d.levels=1;
    RegisteredTexture slot{},unrelated{};
    CHECK(register_texture_record(slot,t,nullptr));CHECK(slot.revision==1);
    CHECK(register_texture_record(unrelated,t,nullptr));uint64_t other=unrelated.revision;
    const uint64_t cached_revision=slot.revision;
    for(int i=0;i<100;i++)CHECK(!register_texture_record(slot,t,nullptr));
    CHECK(slot.revision==cached_revision);
    // Irrelevant snapshot pointer / object magic / inactive layout bytes do not invalidate.
    t.snap=pixels;t.magic=123;t.layout[0]=9;
    CHECK(!register_texture_record(slot,t,nullptr));
    view_t v{};v.base_level=999;v.format=999;
    CHECK(!register_texture_record(slot,t,&v));
    v.set_levels=1;v.base_level=1;v.nlevels=1;
    CHECK(register_texture_record(slot,t,&v));CHECK(slot.revision!=cached_revision);
    uint64_t rev=slot.revision;v.set_levels=2;
    CHECK(!register_texture_record(slot,t,&v));CHECK(slot.revision==rev);
    v.base_level=2;CHECK(register_texture_record(slot,t,&v));
    rev=slot.revision;v.set_layers=1;v.min_layer=1;v.nlayers=1;
    CHECK(register_texture_record(slot,t,&v));CHECK(slot.revision==rev+1);
    v.set_format=1;CHECK(register_texture_record(slot,t,&v));
    v.set_target=1;v.target=3;CHECK(register_texture_record(slot,t,&v));
    CHECK(register_texture_record(slot,t,nullptr));
    CHECK(unrelated.revision==other);
    t.mem=pixels+1;CHECK(register_texture_record(slot,t,nullptr));
    t.mem=pixels;t.L.align=32;CHECK(register_texture_record(slot,t,nullptr));
    t.d.width=2;CHECK(register_texture_record(slot,t,nullptr));
    // CPU texture already uploaded: one check maximum per submission, even on repeated hits.
    CpuImage im;im.cpu_hash=hash_memory(pixels,4);hash_calls=0;
    for(uint64_t submit=0;submit<80;submit++){
        stats.submissions=submit;unsigned before=hash_calls;
        for(int hit=0;hit<100;hit++)check_cpu(im,&t,true);
        CHECK(hash_calls-before<=1);
    }
    CHECK(hash_calls>=4 && hash_calls<16);
    // Stable texture changes must eventually be detected by the original staggered cadence.
    pixels[0]=42;unsigned before=hash_calls;
    for(uint64_t submit=80;submit<96 && im.uploaded;submit++){
        stats.submissions=submit;check_cpu(im,&t,true);
    }
    CHECK(!im.uploaded && im.stable_checks==0);CHECK(hash_calls==before+1);
    im.uploaded=true;im.gpu_written=true;before=hash_calls;
    stats.submissions=100;check_cpu(im,&t,true);CHECK(hash_calls==before);
    CHECK(!memo_eligible(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE));CHECK(memo_eligible(99));
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix='xbg-texture-cpu-') as temp:
        cpp, wasm = pathlib.Path(temp) / 'test.cpp', pathlib.Path(temp) / 'test.wasm'
        cpp.write_text(harness, encoding='utf-8')
        subprocess.run([args.compiler, '--target=wasm32', '-std=c++17', '-ffreestanding',
                        '-fno-exceptions', '-fno-rtti', '-nostdlib', '-Wl,--no-entry',
                        '-Wl,--export=run_tests', '-Wl,--export-memory', str(cpp), '-o', str(wasm)], check=True)
        js = ('const fs=require("fs");WebAssembly.instantiate(fs.readFileSync(process.argv[1]))'
              '.then(({instance})=>{const n=instance.exports.run_tests();'
              'if(n)throw Error("CPU regression failed at generated C++ line "+n);'
              'console.log("PASS: production registration semantics, local revision isolation, "'
              '+"bounded CPU hash/change detection, storage-image exclusion; no Vulkan executed");});')
        subprocess.run([args.node, '-e', js, str(wasm)], check=True)


if __name__ == '__main__':
    main()
