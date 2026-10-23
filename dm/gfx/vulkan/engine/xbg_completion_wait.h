// 完成感知争锁：同一 completed 原子值、短临界区通知及固定总预算。
// Completion-aware lock acquisition: one completed atomic, short notification sections, and one total budget.
#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace xbg_completion {
using Clock = std::chrono::steady_clock;

class Deadline {
    uint64_t timeout_ns_;
    Clock::time_point start_;
    uint64_t remaining_at(Clock::time_point now) const {
        if(timeout_ns_==UINT64_MAX)return UINT64_MAX;
        const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(now-start_).count();
        return elapsed<=0?timeout_ns_:uint64_t(elapsed)>=timeout_ns_?0:timeout_ns_-uint64_t(elapsed);
    }
public:
    // 不把 uint64 总超时转成 signed chrono；仅最多 1 ms 的休眠片段需要转换。
    // Never cast the uint64 total timeout to signed chrono; only the at-most-1-ms sleep slice is converted.
    explicit Deadline(uint64_t timeout_ns):timeout_ns_(timeout_ns),start_(timeout_ns==UINT64_MAX?Clock::time_point{}:Clock::now()){}
    uint64_t remaining_ns() const {return timeout_ns_==UINT64_MAX?UINT64_MAX:remaining_at(Clock::now());}
    bool expired() const {return remaining_ns()==0;}
    Clock::time_point retry_at() const {
        const auto now=Clock::now();
        return now+std::chrono::nanoseconds(std::min<uint64_t>(remaining_at(now),1000000));
    }
};

enum class Acquire { EngineLocked, Completed, TimedOut };
struct WaitStats {uint64_t ns=0,calls=0;};

class Completion {
    std::atomic<uint64_t> &completed_;
    std::mutex mu_;
    std::condition_variable cv_;
    WaitStats pending_;
public:
    explicit Completion(std::atomic<uint64_t> &completed):completed_(completed){}
    void publish(uint64_t value) {
        // 调用者仍持 engine，写回完成条件/发布顺序由调用者保持；保留原 seq_cst。
        // The caller still holds engine and preserves writeback conditions/order; retain the original seq_cst.
        bool advanced;
        {std::lock_guard<std::mutex> lock(mu_);advanced=value>completed_.load();completed_.store(value);}
        if(advanced)cv_.notify_all();
    }
    void record_gpu_wait(uint64_t ns) {
        std::lock_guard<std::mutex> lock(mu_);pending_.ns+=ns;++pending_.calls;
    }
    WaitStats take_gpu_waits() {
        std::lock_guard<std::mutex> lock(mu_);const WaitStats out=pending_;pending_={};return out;
    }
    // before_sleep 仅用于并发测试在复查谓词与挂起之间安排发布；生产调用为空操作。
    // before_sleep only schedules test publication between predicate recheck and suspension; production passes a no-op.
    template<class BeforeSleep>
    Acquire acquire(std::unique_lock<std::recursive_mutex> &engine,uint64_t target,const Deadline &deadline,BeforeSleep before_sleep) {
        for(;;){
            if(completed_.load()>=target)return Acquire::Completed;
            if(deadline.expired())return Acquire::TimedOut;
            if(engine.try_lock())return Acquire::EngineLocked;
            {
                std::unique_lock<std::mutex> lock(mu_);
                if(completed_.load()>=target)return Acquire::Completed;
                if(deadline.expired())return Acquire::TimedOut;
                before_sleep();
                cv_.wait_until(lock,deadline.retry_at(),[&]{return completed_.load()>=target;});
            }
            // 必须先放 completion 小锁再试 engine；无发布者时也定期尝试退休，不能永久等通知。
            // Release the completion lock before trying engine; bounded retries permit retirement even without a publisher.
        }
    }
    Acquire acquire(std::unique_lock<std::recursive_mutex> &engine,uint64_t target,const Deadline &deadline) {
        return acquire(engine,target,deadline,[]{});
    }
};
}
