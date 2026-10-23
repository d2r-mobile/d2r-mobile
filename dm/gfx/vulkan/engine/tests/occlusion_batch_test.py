"""Execute production query helpers in a mock command stream (CPU/WASM only).

Extracts the production begin/end helpers and checks their integration sites.
The mock supplies sample values, pool allocation and report-prefix bookkeeping;
it does not execute Vulkan, shaders, the full draw function or a driver.
"""
import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

sys.dont_write_bytecode = True
from texture_cache_regression import balanced


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--compiler', required=True)
    ap.add_argument('--node', default='node')
    args = ap.parse_args()
    source = (pathlib.Path(__file__).resolve().parent.parent / 'xbg_game.cpp').read_text(encoding='utf-8')
    declaration_start = source.index('struct OpenOcclusion ')
    declaration = source[declaration_start:source.index('open_occlusion;', declaration_start) + len('open_occlusion;')]
    end = balanced(source, source.index('void end_occlusion_query('))
    begin = balanced(source, source.index('void begin_occlusion_query('))
    endpass = balanced(source, source.index('void end_open_pass('))
    assert endpass.index('end_occlusion_query(') < endpass.index('vkCmdEndRenderPass(')
    reset = balanced(source, source.index('case OP_RESET_COUNTER:'))
    assert reset.index('end_occlusion_query(') < reset.index('++counter_epoch')
    report = balanced(source, source.index('case OP_REPORT_COUNTER:'))
    # 样本报告在任何后续 draw 之前用非 draw 的 Commands 结束计数段；时间戳报告不结束 pass，以免切开 tiling pass。
    # A samples report ends the open run with a non-draw Commands before any later draw; a timestamp report keeps the pass,
    # so that it does not split a tiling pass.
    assert 'else{Commands c;' in report and report.index('epochs[counter_epoch].size()') < report.index('else{Commands c;')
    commands = balanced(source, source.index('struct Commands '))
    assert 'end_open_pass()' in commands
    clear = balanced(source, source.index('void clear_attachment('))
    assert clear.index('end_occlusion_query(') < clear.index('masked.encode(')
    draw = balanced(source, source.index('void draw(int prim'))
    assert 'occlusion_batch_enabled' in draw
    assert 'begin_occlusion_query(' in draw and 'end_occlusion_query(' in draw
    assert 'vkCmdBeginQuery(' not in draw and 'vkCmdEndQuery(' not in draw
    prefix = r'''
typedef __UINT32_TYPE__ uint32_t;typedef __UINT64_TYPE__ uint64_t;
typedef unsigned VkCommandBuffer;typedef unsigned VkQueryPool;
constexpr unsigned VK_NULL_HANDLE=0,VK_QUERY_CONTROL_PRECISE_BIT=1;
int failure=0;void need(bool ok,const char*){if(!ok)failure=1;}
struct Record{unsigned epoch=0;uint64_t value=0;bool allocated=false,ended=false;};
Record records[8192];unsigned current_epoch=0,current_id=0,active_id=0,active_cmd=0;
unsigned begins=0,ends=0,pass_cmd=0,pass_id=0,used[2]={},growth=0;
uint64_t occlusion_queries_started=0;
uint64_t expected[4]={};
void vkCmdBeginQuery(unsigned cmd,unsigned pool,unsigned index,unsigned flags){
 if(active_id || !cmd || pass_cmd!=cmd || flags!=1)failure=2;
 unsigned id=pool*256+index;if(id>=8192 || !records[id].allocated || records[id].ended)failure=3;
 active_id=id;active_cmd=cmd;++begins;
}
void vkCmdEndQuery(unsigned cmd,unsigned pool,unsigned index){
 unsigned id=pool*256+index;if(active_id!=id || active_cmd!=cmd || pass_cmd!=cmd)failure=4;
 records[id].ended=true;active_id=active_cmd=0;++ends;
}
struct OpenPass{unsigned cmd=0;bool depth_attached=false,depth_used=false;} open_pass;
void pass_prof_end(unsigned){}
// end_open_pass's per-op pass-end and idle-depth counters (diagnostics only).
uint64_t depth_idle_passes=0,pass_end_by[64]={};unsigned cur_op=0;void pass_prof_depth_idle(unsigned){}
void vkCmdEndRenderPass(unsigned cmd){if(active_id || pass_cmd!=cmd)failure=7;}
'''
    model = r'''
void close_pass(){end_open_pass();if(active_id)failure=5;pass_cmd=pass_id=0;}
void ensure_pass(unsigned cmd,unsigned pass){if(pass_cmd!=cmd || pass_id!=pass){close_pass();pass_cmd=cmd;pass_id=pass;open_pass.cmd=cmd;}}
void draw(unsigned slot,unsigned pass,unsigned samples,bool batch){
 unsigned cmd=slot+1;
 if(pass_cmd!=cmd || pass_id!=pass)close_pass();
 unsigned pool=0,index=0;
 if(!open_occlusion.cmd){
  unsigned n=used[slot]++;index=n%256;pool=1+slot*12+n/256;
  if(index==0){close_pass();++growth;} // production pool growth resets outside the pass
  current_id=pool*256+index;records[current_id].allocated=true;records[current_id].epoch=current_epoch;
 }
 ensure_pass(cmd,pass);
 if(!open_occlusion.cmd)begin_occlusion_query(cmd,pool,index);
 if(!active_id)failure=6;else records[active_id].value+=samples;
 expected[current_epoch]+=samples;
 if(!batch)end_occlusion_query();
}
void clear(unsigned synthetic_samples,int control){
 if(control!=1)end_occlusion_query();
 // A backend masked-clear triangle must never contribute to guest counters.
 if(active_id)records[active_id].value+=synthetic_samples;
}
void reset(int control){if(control!=2)end_occlusion_query();++current_epoch;}
bool report(){close_pass();uint64_t sum[4]={};
 for(unsigned i=0;i<8192;i++)if(records[i].allocated){if(!records[i].ended)return false;sum[records[i].epoch]+=records[i].value;}
 for(unsigned e=0;e<=current_epoch;e++)if(sum[e]!=expected[e])return false;
 return !failure;
}
int scenario(bool batch,int control){
 failure=0;current_epoch=current_id=active_id=active_cmd=begins=ends=pass_cmd=pass_id=growth=0;
 used[0]=used[1]=0;open_occlusion={};
 for(auto &r:records)r=Record{};for(auto &v:expected)v=0;
 if(!report())return 10; // zero-draw timestamp/counter report
 for(unsigned i=0;i<813;i++)draw(0,1,1+i%7,batch);
 unsigned ui_segments=begins;if(ui_segments!=(batch?1u:813u))return 11;
 if(!report())return 12; // first prefix excludes all subsequent draws
 draw(0,1,13,batch);draw(0,1,17,batch);
 clear(999,control);draw(0,1,19,batch);
 if(!report())return 13;
 draw(0,2,23,batch);reset(control);draw(0,2,29,batch);
 if(!report())return 14; // reset inside same renderpass keeps old epoch separate
 reset(control);if(!report())return 15; // empty epoch and repeated report
 // Pool growth at 256, renderpass switches and both slots, including reuse.
 for(unsigned i=0;i<600;i++){draw(i&1,10+i,1+i%11,batch);if(i%31==0 && !report())return 16;}
 if(!report())return 17;
 if(begins!=ends || active_id || open_occlusion.cmd)return 18;
 if(growth<4 || used[0]<256 || used[1]<256)return 19;
 return 0;
}
extern "C" int run_tests(int control){
 if(control)return scenario(true,control);
 int a=scenario(true,0);if(a)return a;
 int b=scenario(false,0);return b?100+b:0;
}
'''
    with tempfile.TemporaryDirectory(prefix='xbg-occlusion-batch-') as temp:
        cpp, wasm = pathlib.Path(temp) / 'test.cpp', pathlib.Path(temp) / 'test.wasm'
        cpp.write_text(prefix + declaration + '\n' + end + '\n' + begin + '\n' + endpass + model, encoding='utf-8')
        subprocess.run([args.compiler, '--target=wasm32', '-std=c++17', '-ffreestanding',
                        '-fno-exceptions', '-fno-rtti', '-nostdlib', '-Wl,--no-entry',
                        '-Wl,--export=run_tests', str(cpp), '-o', str(wasm)], check=True)
        js = ('const fs=require("fs");WebAssembly.instantiate(fs.readFileSync(process.argv[1]))'
              '.then(({instance})=>{for(const [mode,want] of [[0,0],[1,13],[2,14]]){'
              'let got=instance.exports.run_tests(mode);if(got!==want)'
              'throw Error(`mode ${mode}: got ${got}, expected ${want}`);'
              'console.log(`mode ${mode}: ${got} (expected ${want})`);}});')
        subprocess.run([args.node, '-e', js, str(wasm)], check=True)
    print('PASS: production begin/end helpers; batched and per-draw prefixes/epochs, clear exclusion, pool256 and slots; negative controls caught. CPU mocks only, no Vulkan/device.')


if __name__ == '__main__':
    main()
