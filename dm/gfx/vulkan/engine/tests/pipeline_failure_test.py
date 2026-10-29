#!/usr/bin/env python3
"""Run the production pipeline guard, lookup and dispatch with a rejecting driver."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def block(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth, end = 1, opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1] / "xbg_game.cpp")
parser.add_argument("--cxx", default="c++")
args = parser.parse_args()
source = args.source.read_text()
harness = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using VkPipeline=uint64_t;
using VkResult=int;
constexpr VkResult VK_SUCCESS=0;
constexpr VkPipeline VK_NULL_HANDLE=0;
enum {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO=1,
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      VK_STRUCTURE_TYPE_MEMORY_BARRIER,
      VK_SHADER_STAGE_COMPUTE_BIT, VK_PIPELINE_BIND_POINT_COMPUTE,
      VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
      VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR};
struct Stage {int sType=0; uint32_t stage=0; uint64_t module=0; const char *pName=nullptr;};
struct VkComputePipelineCreateInfo {int sType=0; uint32_t flags=0; Stage stage{}; uint64_t layout=0;};
struct VkGraphicsPipelineCreateInfo {uint32_t flags=0,stageCount=0; Stage *pStages=nullptr;};
struct VkMemoryBarrier {int sType=0; uint32_t srcAccessMask=0,dstAccessMask=0;};
struct XbgPipelineKey {uint32_t words[4]{},count=0;};
struct PipelineLease {VkPipeline pipeline=0; bool transient=false;};
struct PipelineEntry {VkPipeline pipeline; uint64_t last_used;};
struct Shader {std::string key; uint64_t module;};
struct {Shader *shader[6]{};} state;
struct {int device=1; std::string shader_directory;} cfg;
struct {struct {uint32_t maxComputeWorkGroupCount[3]{1024,1024,1024};} limits;} properties;
std::unordered_map<std::string,PipelineEntry> pipeline_objects;
std::unordered_map<int,VkPipeline> pipeline_fast;
struct {int hits=0,misses=0,bypass=0,evictions=0;} pipeline_counts;
uint64_t pipeline_tick=0,compile_ns=1000;
constexpr size_t pipeline_object_limit=64;
int pipeline_cache=0,pipelines_unsaved=0;
bool pipe_stats=false,pipe_memo_valid=false;
namespace hud {uint64_t pipelines=0,pipeline_ns=0;}
namespace warm {
std::mutex mu; std::unordered_map<uint64_t,int> ids; std::vector<std::string> id_keys;
uint64_t hits=0,async_skips=0,creates=0,create_ns=0,create_max_ns=0;
bool take(const std::string &,VkPipeline &) {return false;}
bool async_request(const VkGraphicsPipelineCreateInfo &,const XbgPipelineKey &,const std::string &) {return false;}
void record(const VkGraphicsPipelineCreateInfo &,const XbgPipelineKey &) {}
}
int create_calls=0,bind_calls=0,dispatch_calls=0,finished=0;
VkResult driver_result=-13;
VkResult vkCreateComputePipelines(int,int,int,const VkComputePipelineCreateInfo *,void *,VkPipeline *out) {
    ++create_calls; *out=77; return driver_result;
}
VkResult vkCreateGraphicsPipelines(int,int,int,const VkGraphicsPipelineCreateInfo *,void *,VkPipeline *out) {
    *out=88; return VK_SUCCESS;
}
void vkDestroyPipeline(int,VkPipeline,void *) {}
template<class F> void retire_handle(F f) {f();}
void pstat_log(VkPipeline) {}
uint64_t timing_clock() {static uint64_t t=0; return ++t;}
void xbg_logf(const char *,...) {}
void provenance_log(const char *,...) {}
void need(bool ok,const char *message) {if(!ok)throw std::runtime_error(message);}
#define CPU_SCOPE(name) (void)0
struct p_dispatch {int x,y,z;};
struct Commands {int command=1; void finish() {++finished;}};
struct Definition {int view() const {return 0;}};
struct Entry {Definition definition;};
struct Descriptors {
    Entry own; Entry *entry=&own; uint64_t pipeline_layout=1,set=1;
    std::vector<uint32_t> dynamic_offsets;
    void defer_writeback() {}
} descriptor_state;
Descriptors &acquire_descriptors() {return descriptor_state;}
void descriptors(Descriptors &,Commands &,bool) {}
void preflight_cpu_reads() {}
bool xbg_compute_pipeline_key(XbgPipelineKey &key,const VkComputePipelineCreateInfo &info,int) {
    key.count=1; key.words[0]=uint32_t(info.stage.module); return true;
}
void vkCmdBindPipeline(int,int,VkPipeline pipeline) {assert(pipeline); ++bind_calls;}
template<class... T> void vkCmdBindDescriptorSets(T...) {}
void vkCmdDispatch(int,int,int,int) {++dispatch_calls;}
template<class... T> void vkCmdPipelineBarrier(T...) {}
int pass_prof_compute_begin(int,const p_dispatch &) {return 0;}
void pass_prof_complete(int,int) {}
namespace bright {void after_dispatch(Commands &) {}}
struct {int dispatches=0;} perf,stats;
'''
# Preserve the renderer's compact layout without treating GCC's layout warning
# as a failure of these runtime checks. Keep other warnings fatal.
harness += '\n#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wmisleading-indentation"\n'
for marker in ("namespace pipeguard{\nstd::mutex", "void pipeline_lookup(", "void dispatch(const p_dispatch &p)"):
    harness += block(source, marker) + "\n"
harness += '\n#pragma GCC diagnostic pop\n'
harness += r'''
int main(int argc,char **argv) {
    assert(argc==2); cfg.shader_directory=std::string(argv[1])+"/shaders";
    Shader rejected{"rejected",10},working{"working",11};
    state.shader[5]=&rejected;
    dispatch({1,1,1});
    assert(create_calls==1 && bind_calls==0 && dispatch_calls==0 && finished==1);
    assert(pipeline_objects.empty() && hud::pipelines==0);
    assert(pipeguard::skipped("c:rejected"));
    assert(pipeguard::lines(pipeguard::base()+"/gfx-pipe-skip")==std::vector<std::string>{"c:rejected"});
    assert(!std::filesystem::exists(pipeguard::marker()));
    dispatch({1,1,1});
    assert(create_calls==1 && finished==1);
    pipeguard::skip.clear(); pipeguard::told.clear(); pipeguard::loaded=false;
    dispatch({1,1,1});
    assert(create_calls==1 && bind_calls==0 && dispatch_calls==0);
    assert(!pipeguard::skipped(pipeguard::graphics_key({"rejected","fragment"})));
    state.shader[5]=&working; driver_result=VK_SUCCESS;
    dispatch({1,1,1});
    assert(create_calls==2 && bind_calls==1 && dispatch_calls==1);
    assert(pipeline_objects.size()==1 && hud::pipelines==1);
    dispatch({1,1,1});
    assert(create_calls==2 && bind_calls==2 && dispatch_calls==2);
    assert(perf.dispatches==2 && stats.dispatches==2);
    std::puts("PASS rejected compute pipeline: no null bind/dispatch/cache, persisted skip, unrelated shaders and cache reuse");
}
'''
with tempfile.TemporaryDirectory(prefix="dm-pipeline-failure-") as temp:
    path = Path(temp)
    (path / "test.cpp").write_text(harness)
    subprocess.run([args.cxx, "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test"), str(path)], check=True, timeout=15)
