#include "frame_timing.h"
static int display_timing_enabled,display_pacing_enabled;
static PFN_vkGetPastPresentationTimingGOOGLE get_past_timing;
static PFN_vkGetRefreshCycleDurationGOOGLE get_refresh_duration;
static dm_frame_window presented_window,submitted_window;
static uint64_t display_refresh_ns,display_phase_ns,display_target_ns,display_poll_ns,display_report_ns;

static void display_timing_reset(void){
    memset(&presented_window,0,sizeof presented_window);memset(&submitted_window,0,sizeof submitted_window);
    display_phase_ns=display_target_ns=display_poll_ns=display_report_ns=0;
    display_refresh_ns=0;
    if(get_refresh_duration&&swapchain){VkRefreshCycleDurationGOOGLE duration={0};
        if(get_refresh_duration(device,swapchain,&duration)==VK_SUCCESS)display_refresh_ns=duration.refreshDuration;}
}
static void display_timing_load(void){
    get_past_timing=NULL;get_refresh_duration=NULL;
    if(display_timing_enabled){
        get_past_timing=(PFN_vkGetPastPresentationTimingGOOGLE)vkGetDeviceProcAddr(device,"vkGetPastPresentationTimingGOOGLE");
        get_refresh_duration=(PFN_vkGetRefreshCycleDurationGOOGLE)vkGetDeviceProcAddr(device,"vkGetRefreshCycleDurationGOOGLE");
    }
    display_timing_enabled=get_past_timing&&get_refresh_duration;
    display_pacing_enabled=1;
#ifdef __ANDROID__
    char value[PROP_VALUE_MAX];
    if(__system_property_get("debug.d2r.display_pacing",value)>0)display_pacing_enabled=!strcmp(value,"1");
#endif
    LOG("[vulkan-display-timing] available=%d pacing=%d",display_timing_enabled,display_pacing_enabled);
}
static int display_timing_compare(const void *a,const void *b){
    uint64_t x=((const VkPastPresentationTimingGOOGLE*)a)->actualPresentTime;
    uint64_t y=((const VkPastPresentationTimingGOOGLE*)b)->actualPresentTime;return (x>y)-(x<y);
}
static void display_timing_poll(uint64_t now){
    dm_frame_push(&submitted_window,now);
    // Android retains ten records and only exposes frames at least five presents old.
    if(display_timing_enabled&&swapchain){
        uint32_t count=0;
        if(get_past_timing(device,swapchain,&count,NULL)==VK_SUCCESS&&count){
            VkPastPresentationTimingGOOGLE values[64];if(count>64)count=64;
            VkResult result=get_past_timing(device,swapchain,&count,values);
            if(result==VK_SUCCESS||result==VK_INCOMPLETE){
                qsort(values,count,sizeof *values,display_timing_compare);
                for(unsigned i=0;i<count;i++)if(values[i].actualPresentTime){
                    dm_frame_push(&presented_window,values[i].actualPresentTime);
                    display_phase_ns=values[i].actualPresentTime;
                }
            }
        }
        if(now-display_poll_ns>=250000000ull){
            display_poll_ns=now;VkRefreshCycleDurationGOOGLE duration={0};
            if(get_refresh_duration(device,swapchain,&duration)==VK_SUCCESS)display_refresh_ns=duration.refreshDuration;
        }
    }
    if(now-display_report_ns>=500000000ull){
        display_report_ns=now;
        dm_frame_summary actual=dm_frame_summarize(&presented_window,now);
        dm_frame_summary chosen=actual.intervals?actual:dm_frame_summarize(&submitted_window,now);
        pthread_mutex_lock(&debug_lock);
        debug_stats.timing_updated_ns=now;
        debug_stats.presented_valid=actual.intervals!=0;debug_stats.timing_valid=chosen.intervals!=0;
        debug_stats.measured_fps=chosen.fps;debug_stats.p95_ms=chosen.p95_ms;debug_stats.max_ms=chosen.max_ms;
        pthread_mutex_unlock(&debug_lock);
        LOG("[vulkan-display-stats] source=%s fps=%.3f p95=%.3f max=%.3f samples=%u refresh_ns=%llu",actual.intervals?"presented":"submitted",chosen.fps,chosen.p95_ms,chosen.max_ms,chosen.intervals,(unsigned long long)display_refresh_ns);
    }
}
