#pragma once
#include <stdint.h>
#include <stdlib.h>

static inline int dm_frame_abnormal(uint64_t interval, uint64_t cpu, uint64_t period) {
    uint64_t threshold = period + period / 5;
    if (threshold < 20000000) threshold = 20000000;
    return interval >= threshold || cpu >= threshold;
}

typedef struct { uint64_t times[1024]; unsigned next, count; } dm_frame_window;
typedef struct { double fps, p95_ms, max_ms; unsigned intervals; } dm_frame_summary;
static inline void dm_frame_push(dm_frame_window *w, uint64_t time) {
    if (!time || (w->count && time <= w->times[(w->next + 1023) % 1024])) return;
    w->times[w->next] = time; w->next = (w->next + 1) % 1024;
    if (w->count < 1024) w->count++;
}
static inline int dm_frame_compare(const void *a, const void *b) {
    uint64_t x=*(const uint64_t *)a,y=*(const uint64_t *)b; return (x>y)-(x<y);
}
static inline dm_frame_summary dm_frame_summarize(const dm_frame_window *w, uint64_t now) {
    dm_frame_summary out={0};
    if(w->count<2)return out;
    uint64_t last=w->times[(w->next+1023)%1024];
    if(now<last || now-last>1000000000ull)return out;
    uint64_t intervals[1023],sum=0;
    for(unsigned i=1;i<w->count;i++){
        uint64_t b=w->times[(w->next+1024-i)%1024],a=w->times[(w->next+1023-i)%1024];
        if(last-a>2000000000ull)break;
        intervals[out.intervals++]=b-a;sum+=b-a;
    }
    if(!out.intervals||!sum)return out;
    qsort(intervals,out.intervals,sizeof *intervals,dm_frame_compare);
    out.fps=out.intervals*1e9/sum;
    out.p95_ms=intervals[(out.intervals*95+99)/100-1]/1e6;
    out.max_ms=intervals[out.intervals-1]/1e6;
    return out;
}
static inline uint64_t dm_frame_deadline(uint64_t *last, uint64_t now, uint64_t period,
                                        uint64_t refresh, uint64_t phase) {
    if(!period||!refresh){*last=0;return 0;}
    uint64_t ticks=(period+refresh/2)/refresh;if(!ticks)ticks=1;
    uint64_t step=ticks*refresh,target=*last?*last+step:0,earliest=now+refresh;
    if(target<earliest){
        uint64_t origin=phase?phase:now;
        target=earliest<=origin?origin:origin+((earliest-origin+refresh-1)/refresh)*refresh;
    }
    *last=target;return target;
}
