#!/usr/bin/env python3
"""Execute the production reset worker and retirement wait with controlled Vulkan results."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def block(text, marker):
    start = text.index(marker)
    opening = text.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1] / "xbg_game.cpp")
parser.add_argument("--cxx", default="c++")
args = parser.parse_args()
source = args.source.read_text()
worker = block(source, "namespace poolreset {")
# Execute retirement through its fence wait and deferred reset; the rest handles readbacks.
retirement = source[source.index("void retire_slot(Slot &s){"):source.index("    ++perf.waits;", source.index("void retire_slot(Slot &s){"))] + "}\n"
harness = r'''
#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
using VkResult=int; using VkFence=uint64_t; using VkCommandPool=uint64_t;
constexpr int VK_SUCCESS=0, VK_NOT_READY=1, VK_TIMEOUT=2, VK_ERROR_DEVICE_LOST=-4, VK_TRUE=1;
struct {int device=0; uint64_t fence_timeout_ns=0;} cfg;
struct Slot {bool pending=true, reset_queued=false, reset_busy=false; VkResult reset_result=0; VkFence fence=1; VkCommandPool pool=2;};
const std::thread::id render_thread=std::this_thread::get_id();
VkResult background_result, foreground_result, reset_result;
std::atomic<int> resets{0}; std::atomic<bool> gpu_complete{false};
VkResult vkWaitForFences(int,int,const VkFence*,int,uint64_t) {
    VkResult r=std::this_thread::get_id()==render_thread?foreground_result:background_result;
    if(r==VK_SUCCESS)gpu_complete=true;
    return r;
}
VkResult vkResetCommandPool(int,VkCommandPool,int) {assert(gpu_complete); ++resets; return reset_result;}
VkResult vkGetFenceStatus(int,VkFence) {return gpu_complete?VK_SUCCESS:VK_NOT_READY;}
bool ab_off(const char*) {return false;}
void xbg_logf(const char*,...) {}
uint64_t timing_clock() {return uint64_t(std::chrono::steady_clock::now().time_since_epoch().count());}
uint64_t block_ns[1]{}, block_calls[1]{}; int sync_site=0;
#define CPU_SCOPE(name) (void)0
#define VK(call) do {if((call)!=VK_SUCCESS)throw std::runtime_error("Vulkan operation failed");} while(0)
'''
harness += worker + "\n" + retirement + r'''
void run_case(VkResult bg, VkResult fg, VkResult reset, int expected_resets, VkResult expected_result, bool must_fail=false) {
    background_result=bg; foreground_result=fg; reset_result=reset; resets=0; gpu_complete=false;
    Slot slot; poolreset::enabled=true; poolreset::start(); poolreset::queue(slot);
    bool failed=false;
    try {retire_slot(slot);} catch(const std::runtime_error&) {failed=true;}
    poolreset::drain();
    assert(failed==must_fail);
    assert(resets==expected_resets);
    assert(slot.reset_result==expected_result);
}
int main() {
    run_case(VK_TIMEOUT,VK_SUCCESS,VK_SUCCESS,1,VK_SUCCESS);
    run_case(VK_SUCCESS,VK_SUCCESS,VK_SUCCESS,1,VK_SUCCESS);
    run_case(VK_TIMEOUT,VK_TIMEOUT,VK_SUCCESS,0,VK_TIMEOUT,true);
    run_case(VK_ERROR_DEVICE_LOST,VK_ERROR_DEVICE_LOST,VK_SUCCESS,0,VK_ERROR_DEVICE_LOST,true);
    run_case(VK_TIMEOUT,VK_SUCCESS,VK_ERROR_DEVICE_LOST,1,VK_ERROR_DEVICE_LOST);
    std::puts("PASS command pool reset: recovered timeout, normal reset, pending GPU, device loss, reset failure");
}
'''
with tempfile.TemporaryDirectory(prefix="dm-pool-reset-") as temp:
    path = Path(temp)
    (path / "test.cpp").write_text(harness)
    subprocess.run([args.cxx, "-std=c++17", "-pthread", "-Wall", "-Wextra", "-Werror",
                    str(path / "test.cpp"), "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True, timeout=15)
