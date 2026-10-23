// 同一完成感知 helper 的并发测试；仅显式 debug 宏编入，启动环境开关决定是否运行。
// Concurrent tests of the production completion helper; compiled only by an explicit debug macro and run by a startup env switch.
#ifdef DM_COMPLETION_WAIT_SELFTEST
#include "../vulkan/engine/xbg_completion_wait.h"
#include <cstdio>
#include <initializer_list>
#include <thread>

namespace {
using xbg_completion::Acquire;
using xbg_completion::Completion;
using xbg_completion::Deadline;
using xbg_completion::Clock;
unsigned assertions=0;
std::atomic<uint64_t> max_wait_ns{0};
bool verify(std::initializer_list<bool> checks){
    bool ok=true;for(bool check:checks){++assertions;ok=check&&ok;}return ok;
}
template<class BeforeSleep>
Acquire observed_acquire(Completion &c,std::unique_lock<std::recursive_mutex> &lock,uint64_t target,const Deadline &deadline,BeforeSleep before_sleep){
    const auto begin=Clock::now();const auto result=c.acquire(lock,target,deadline,before_sleep);
    const auto ns=uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now()-begin).count());
    auto previous=max_wait_ns.load();while(previous<ns&&!max_wait_ns.compare_exchange_weak(previous,ns)){}
    return result;
}
Acquire observed_acquire(Completion &c,std::unique_lock<std::recursive_mutex> &lock,uint64_t target,const Deadline &deadline){
    return observed_acquire(c,lock,target,deadline,[]{});
}
struct Gate {
    std::mutex mu;std::condition_variable cv;bool set_=false;
    void set(){std::lock_guard<std::mutex> lock(mu);set_=true;cv.notify_all();}
    bool wait(){std::unique_lock<std::mutex> lock(mu);return cv.wait_for(lock,std::chrono::seconds(2),[&]{return set_;});}
};

bool already_complete(){
    std::atomic<uint64_t> done{0};Completion c(done);std::recursive_mutex engine;
    c.publish(3);std::unique_lock<std::recursive_mutex> lock(engine,std::defer_lock);
    const auto complete=observed_acquire(c,lock,3,Deadline(0));const bool complete_owned=lock.owns_lock();
    const auto expired=observed_acquire(c,lock,4,Deadline(0));
    return verify({complete==Acquire::Completed,!complete_owned,expired==Acquire::TimedOut,!lock.owns_lock()});
}

bool publication_window_once(bool hold_after_completion){
    std::atomic<uint64_t> done{0};Completion c(done);std::recursive_mutex engine;
    std::unique_lock<std::recursive_mutex> owner(engine);Gate checked,publishing,finished;
    int payload=0,seen=0;bool owned=true,gate_ok=true;Acquire result=Acquire::TimedOut;
    std::thread waiter([&]{
        std::unique_lock<std::recursive_mutex> lock(engine,std::defer_lock);
        result=observed_acquire(c,lock,1,Deadline(1500000000),[&]{checked.set();if(!publishing.wait())gate_ok=false;});
        owned=lock.owns_lock();if(result==Acquire::Completed)seen=payload;finished.set();
    });
    const bool ready=checked.wait();publishing.set();payload=0x5a23;c.publish(1);
    const bool finished_while_locked=finished.wait();
    // 完成发布后仍持 engine 40 ms，模拟后续提交；等待者必须已经不持 engine 返回。
    // Hold engine another 40 ms after publication, modeling a later submit; the waiter must already have returned without engine.
    if(hold_after_completion)std::this_thread::sleep_for(std::chrono::milliseconds(40));owner.unlock();waiter.join();
    return verify({ready,gate_ok,finished_while_locked,result==Acquire::Completed,!owned,seen==0x5a23});
}
bool publication_window_and_visibility(){
    // 每轮用门闩固定复查到挂起的窗口；有限循环不依赖 sleep 命中竞态。
    // Every round gates the predicate-to-suspension window; the finite loop does not rely on sleep to hit a race.
    for(unsigned i=0;i<32;i++)if(!publication_window_once(i==0))return false;
    return true;
}

bool no_publisher_release(){
    std::atomic<uint64_t> done{0};Completion c(done);std::recursive_mutex engine;
    std::unique_lock<std::recursive_mutex> owner(engine);Gate sleeping;Acquire result=Acquire::TimedOut;bool owned=false;
    std::thread waiter([&]{std::unique_lock<std::recursive_mutex> lock(engine,std::defer_lock);
        result=observed_acquire(c,lock,1,Deadline(1500000000),[&]{sleeping.set();});owned=lock.owns_lock();});
    const bool ready=sleeping.wait();owner.unlock();waiter.join();
    return verify({ready,result==Acquire::EngineLocked,owned,done.load()==0});
}

bool finite_timeout(){
    std::atomic<uint64_t> done{0};Completion c(done);std::recursive_mutex engine;
    std::unique_lock<std::recursive_mutex> owner(engine);Gate finished;Acquire result=Acquire::Completed;bool owned=true,expired=false;
    std::thread waiter([&]{Deadline deadline(20000000);std::unique_lock<std::recursive_mutex> lock(engine,std::defer_lock);
        result=observed_acquire(c,lock,1,deadline);owned=lock.owns_lock();expired=deadline.expired();finished.set();});
    const bool before_unlock=finished.wait();owner.unlock();waiter.join();
    return verify({before_unlock,result==Acquire::TimedOut,!owned,expired});
}

bool shared_budget_and_large_timeout(){
    std::atomic<uint64_t> done{0};Completion c(done);std::recursive_mutex engine;
    std::unique_lock<std::recursive_mutex> lock(engine,std::defer_lock);Deadline budget(100000000);
    if(!verify({observed_acquire(c,lock,1,budget)==Acquire::EngineLocked}))return false;
    lock.unlock();std::this_thread::sleep_for(std::chrono::milliseconds(120));
    if(!verify({observed_acquire(c,lock,1,budget)==Acquire::TimedOut,!lock.owns_lock()}))return false;
    Deadline huge(UINT64_MAX-1),infinite(UINT64_MAX);
    return verify({huge.remaining_ns()>UINT64_MAX/2,infinite.remaining_ns()==UINT64_MAX});
}

bool multiple_targets_and_delayed_writeback(){
    std::atomic<uint64_t> done{0};Completion c(done);std::recursive_mutex engine;
    std::unique_lock<std::recursive_mutex> owner(engine);Gate sleeping[4],finished[4];std::thread waiters[4];
    Acquire results[4];int payload[2]={0,0},seen[4]={};bool owned[4]={};std::atomic<unsigned> returned{0};
    for(unsigned i=0;i<4;i++)waiters[i]=std::thread([&,i]{
        std::unique_lock<std::recursive_mutex> lock(engine,std::defer_lock);
        results[i]=observed_acquire(c,lock,i<2?1:2,Deadline(1500000000),[&]{sleeping[i].set();});
        owned[i]=lock.owns_lock();if(results[i]==Acquire::Completed)seen[i]=payload[i<2?0:1];++returned;finished[i].set();
    });
    bool ready=true;for(auto &gate:sleeping)ready=gate.wait()&&ready;
    // GPU-ready 或较小目标发布不能代替尚未完成的CPU写回；真正完成后才发布对应 fence。
    // GPU readiness or a smaller target cannot replace pending CPU writeback; publish each fence only after its data is ready.
    c.publish(0);std::this_thread::sleep_for(std::chrono::milliseconds(4));const bool none_early=returned.load()==0;
    payload[0]=17;c.publish(1);const bool low_done=finished[0].wait()&&finished[1].wait();
    const bool high_pending=returned.load()==2;payload[1]=29;c.publish(2);
    bool all_done=true;for(auto &gate:finished)all_done=gate.wait()&&all_done;
    owner.unlock();for(auto &thread:waiters)thread.join();
    bool valid=true;for(unsigned i=0;i<4;i++)valid=verify({results[i]==Acquire::Completed,!owned[i],seen[i]==(i<2?17:29)})&&valid;
    return verify({ready,none_early,low_done,high_pending,all_done})&&valid;
}

bool paired_statistics(){
    std::atomic<uint64_t> done{0};Completion c(done);Gate start;std::atomic<unsigned> active{4};
    std::thread producers[4];for(auto &thread:producers)thread=std::thread([&]{start.wait();for(int i=0;i<1000;i++)c.record_gpu_wait(7);--active;});
    uint64_t ns=0,calls=0;bool paired=true;start.set();
    while(active.load()){
        const auto part=c.take_gpu_waits();paired=paired && part.ns==part.calls*7;ns+=part.ns;calls+=part.calls;
        std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
    for(auto &thread:producers)thread.join();const auto last=c.take_gpu_waits();ns+=last.ns;calls+=last.calls;
    const auto empty=c.take_gpu_waits();
    return verify({paired,last.ns==last.calls*7,ns==28000,calls==4000,empty.ns==0,empty.calls==0});
}
}

extern "C" int xbg_completion_wait_selftest(char *report,size_t bytes){
    assertions=0;max_wait_ns=0;
    struct Test {const char *name;bool (*run)();};
    const Test tests[]={{"already-complete",already_complete},{"publication-window-and-writeback",publication_window_and_visibility},
        {"no-publisher-release",no_publisher_release},{"finite-timeout",finite_timeout},
        {"shared-budget-large-timeout",shared_budget_and_large_timeout},{"multiple-targets-delayed-writeback",multiple_targets_and_delayed_writeback},
        {"paired-statistics",paired_statistics}};
    const auto cases=sizeof(tests)/sizeof(tests[0]);
    for(unsigned i=0;i<cases;i++)if(!tests[i].run()){
        if(report&&bytes)snprintf(report,bytes,"NVN COMPLETION_WAIT_SELFTEST FAIL case=%s cases=%u/%zu assertions=%u max_wait_ms=%.3f",tests[i].name,i+1,cases,assertions,double(max_wait_ns.load())/1e6);return int(i)+1;
    }
    if(report&&bytes)snprintf(report,bytes,"NVN COMPLETION_WAIT_SELFTEST PASS cases=%zu assertions=%u max_wait_ms=%.3f",cases,assertions,double(max_wait_ns.load())/1e6);return 0;
}
// 独立运行（dm/gfx/tests/Makefile 用 -DDM_COMPLETION_WAIT_MAIN 编）：打印报告，失败返回非 0
// Standalone run (built by dm/gfx/tests/Makefile with -DDM_COMPLETION_WAIT_MAIN): prints the report, non-zero on failure
#ifdef DM_COMPLETION_WAIT_MAIN
int main(){char report[256];const int r=xbg_completion_wait_selftest(report,sizeof report);puts(report);return r;}
#endif
#endif
