#include "frame_timing.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void){
    assert(!dm_frame_abnormal(33333333, 10000000, 33333333));
    assert(dm_frame_abnormal(60000000, 10000000, 33333333));
    assert(dm_frame_abnormal(21000000, 10000000, 16666667));
    assert(dm_frame_abnormal(18000000, 21000000, 0));
    dm_frame_window w={0};uint64_t t=1000000000ull;
    for(int i=0;i<100;i++){dm_frame_push(&w,t);t+=33333333;}
    dm_frame_summary s=dm_frame_summarize(&w,t);
    assert(fabs(s.fps-30)<.001 && s.p95_ms>33.3 && s.p95_ms<33.4);
    unsigned n=w.count;dm_frame_push(&w,t-33333333);assert(w.count==n);
    dm_frame_push(&w,t+100000000);s=dm_frame_summarize(&w,t+100000000);
    assert(s.max_ms>130 && s.fps<30);
    assert(!dm_frame_summarize(&w,t+2000000000).intervals);
    for(int i=0;i<2000;i++)dm_frame_push(&w,t+200000000ull+i*10000000ull);
    s=dm_frame_summarize(&w,t+200000000ull+1999*10000000ull);assert(fabs(s.fps-100)<.001);
    uint64_t deadline=0,r=8333333,now=1000000000;
    uint64_t a=dm_frame_deadline(&deadline,now,33333333,r,now);
    uint64_t b=dm_frame_deadline(&deadline,now+r,33333333,r,now);
    assert(b-a==4*r);
    uint64_t c=dm_frame_deadline(&deadline,now+1000000000,33333333,r,now);
    assert(c>=now+1000000000+r && (c-now)%r==0);
    assert(!dm_frame_deadline(&deadline,now,0,r,now)&&!deadline);
    puts("frame_timing_test PASS");
}
