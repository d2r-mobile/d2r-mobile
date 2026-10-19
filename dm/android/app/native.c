// NativeActivity 工程诊断：APK 域内合成 ARM64 门往返与自有索引纹理 Vulkan 场景，窗口只在就绪回调期间借用，不运行游戏。
// NativeActivity engineering diagnostics: synthetic ARM64 gates plus an owned indexed-textured Vulkan scene inside the APK domain, borrowing windows only while the lifecycle is ready; never runs the game.
#include "../artifact.h"
#include "../vulkan/vk_host.h"
#include "../vulkan/vk_indexed_textured.h"
#include "../vulkan/vk_swapchain.h"
#include "diagnostic_scene.h"
#include "input_policy.h"
#include "lifecycle.h"
#include "present_policy.h"
#include "input_source.h"
#include "gamepad_events.h"
#include "touch_hud.h"
#include "touch_hud_overlay.h"
#include "view_policy.h"
#include "wsi_bridge.h"
#include "wsi_diagnostic.h"
#include "wsi_frame_result.h"
#include "wsi_report.h"
#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/looper.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"D2R-Diagnostic",__VA_ARGS__)
typedef struct{dm_vk vk;AInputQueue*input;dm_app_lifecycle*lifecycle;ANativeActivity*activity;ANativeWindow*window;dm_input_arbiter*arbiter;dm_view_policy view;dm_input_policy gate;ALooper*looper;int timer_fd;int resumed,focused,touch_allowed;float density,touch_width_pt,touch_height_pt,touch_density;uint64_t last_touch_buttons;int32_t last_touch_lx,last_touch_ly,last_touch_rx,last_touch_ry;int64_t last_touch_log_ns;dm_android_touch_observation hud_obs;dm_touch_hud_frame hud_frame;int hud_obs_valid;dm_gamepad_context gamepad_context;dm_npad_state gamepad_state;uint32_t gamepad_id,gamepad_generation;int gamepad_admitted;int64_t gamepad_after_ns;uint64_t controller_session;jobject controller_boundary;jclass controller_class;jmethodID controller_release;dm_wsi wsi;dm_wsi_bridge wsi_bridge;dm_wsi_report report;dm_vk wsi_vk;dm_vk_swapchain wsi_chain;int wsi_chain_live;int frame_fd;int wsi_last_logged;char vertex[4096],fragment[4096];}app_state;
static app_state *g_controller_app;
static uint64_t g_controller_next_session;
enum{dm_controller_added=0,dm_controller_removed=1,dm_controller_changed=2};
static int64_t monotonic_ns(void){
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts))return -1;
    return (int64_t)ts.tv_sec*1000000000LL+ts.tv_nsec;
}
// #147 有界证据输出：进度行由既有回调路径触发（无新增定时器/唤醒），至多 1 Hz；终态强制一次快照。
// #147 bounded evidence: progress lines ride on existing callback paths (no new timer or wakeup), at most 1 Hz;
// terminal states force one snapshot.
static void wsi_report_now(app_state*s,uint64_t*now){
    (void)s;
    int64_t value=monotonic_ns();
    *now=value<0?0:(uint64_t)value;
}
static void wsi_report_progress(app_state*s){
    if(!s||!s->wsi.enabled)return;
    uint64_t now;wsi_report_now(s,&now);
    if(!dm_wsi_report_due(&s->report,now))return;
    char line[384];dm_wsi_report_progress_line(&s->report,now,line,sizeof line);LOG("%s",line);
}
static void wsi_report_snapshot(app_state*s,const char*reason){
    if(!s||!s->wsi.enabled)return;
    uint64_t now;wsi_report_now(s,&now);
    // 每个终态恰好一次：重复 pause/stop/错误不重复输出。| Exactly once per terminal transition; duplicates stay silent.
    if(!dm_wsi_report_take_snapshot(&s->report,now))return;
    char line[384];dm_wsi_report_progress_line(&s->report,now,line,sizeof line);
    LOG("wsi final snapshot (%s): %s",reason,line);
}
// 真实失败：保留阶段与原始码（未知为 DM_WSI_CODE_UNKNOWN），同一次失败只记一次。
// Real failure: stage and raw code (DM_WSI_CODE_UNKNOWN when unknown), recorded once per failure episode.
static void wsi_report_fail(app_state*s,dm_wsi_stage stage,int code,int unsupported,int open_failure){
    if(!s||!s->wsi.enabled)return;
    if(!dm_wsi_report_failure(&s->report,stage,code,unsupported,open_failure))return;   // 本失败已记过 | already recorded
    char line[320];dm_wsi_report_failure_line(&s->report,line,sizeof line);LOG("%s",line);
}
// 真实打开边界：控制器在每次真正 ops.open 前调用，redraw 快路径不会触发。
// Real open boundary: called by the controller before every real ops.open; the redraw fast path never fires it.
static void wsi_open_observed(void*ctx,uint32_t width,uint32_t height){
    app_state*s=ctx;
    dm_wsi_report_attempt(&s->report,width,height);
}
static int copy_asset(ANativeActivity*a,const char*name,char path[4096]){
    int n=snprintf(path,4096,"%s/%s",a->internalDataPath,name);if(n<0||n>=4096)return 1;
    AAsset*asset=AAssetManager_open(a->assetManager,name,AASSET_MODE_STREAMING);if(!asset)return 1;
    off64_t size=AAsset_getLength64(asset);if(size<=0||size>1024*1024){AAsset_close(asset);return 1;}
    FILE*f=fopen(path,"wb");if(!f){AAsset_close(asset);return 1;}int result=0;char buffer[4096];
    while(size>0){size_t want=size<(off64_t)sizeof(buffer)?(size_t)size:sizeof(buffer);int got=AAsset_read(asset,buffer,want);
        if(got<=0||fwrite(buffer,1,(size_t)got,f)!=(size_t)got){result=1;break;}size-=got;}
    if(fclose(f))result=1;AAsset_close(asset);return result;
}
static int native_smoke(ANativeActivity*a){
    char path[4096];if(copy_asset(a,"synthetic.dma",path))return 1;
    dm_rewrite_output input={0};dm_artifact_mappings mapped={0};dm_host*host=NULL;int result=1;
    extern void dm_gate(void);if(dm_artifact_read(path,&input)||dm_artifacts_map(&input,1,(uintptr_t)dm_gate,&mapped))goto done;
    host=dm_host_create(4u*1024u*1024u);if(!host||dm_guest_attach(host,&mapped.gates))goto done;
    typedef uint64_t(*entry)(void);uint64_t value=((entry)(void*)(mapped.modules.mappings[0].base+0x20))();
    result=value!=dm_guest_current()->slots[DM_SLOT_TLS];
    if(dm_guest_detach()){LOG("guest detach failed");abort();}
done:
    if(host&&dm_host_destroy(host))result=1;if(dm_artifacts_unmap(&mapped))result=1;dm_rewrite_free(&input);return result;
}
static int host_load(void*ctx){
    app_state*s=ctx;
    int failed=native_smoke(s->activity);
    LOG("synthetic APK-process ARM64 gate smoke=%s",failed?"FAIL":"PASS");
    if(failed)return 1;
    if(copy_asset(s->activity,"indexed_textured.vert.spv",s->vertex)||
       copy_asset(s->activity,"indexed_textured.frag.spv",s->fragment)){
        LOG("indexed textured shader asset copy failed");return 1;
    }
    return 0;
}
static int host_stop(void*ctx){
    app_state*s=ctx;
    // #143 WSI 排空失败必须传播：不销毁设备、不谎称停止成功。| A failed WSI drain propagates; the device is kept and stop is not claimed.
    if(s->wsi.enabled){
        dm_wsi_report_event(&s->report,DM_WSI_EVENT_STOP);
        if(dm_wsi_stop(&s->wsi)!=DM_WSI_OK){
            LOG("wsi stop failed: presentation ownership retained; host stop reports failure");
            wsi_report_fail(s,DM_WSI_STAGE_DRAIN,DM_WSI_CODE_UNKNOWN,0,0);
            wsi_report_snapshot(s,"stop failed");
            return 1;
        }
        wsi_report_snapshot(s,"stop");
    }
    if(s->vk.device)dm_vk_destroy(&s->vk);
    return 0;
}
static int host_pause(void*ctx){(void)ctx;return 0;}
static int host_resume(void*ctx){(void)ctx;return 0;}
static int host_surface_acquire(void*ctx,void*surface){
    (void)ctx;
    if(!surface)return 1;
    // 在 acquire 之前协商 RGBA_8888：0 宽高沿用 Surface 基础尺寸，不改视图几何；失败则不 acquire，引用不会泄漏。
    // Negotiate RGBA_8888 before acquire: zero width/height keep the Surface base size, so view geometry is untouched;
    // on failure nothing is acquired, so no reference can leak.
    int32_t format_result=ANativeWindow_setBuffersGeometry((ANativeWindow*)surface,0,0,WINDOW_FORMAT_RGBA_8888);
    if(format_result<0){
        LOG("surface format negotiation failed: RGBA_8888 request result=%d; surface not acquired",(int)format_result);
        return 1;
    }
    LOG("surface format negotiated: RGBA_8888 result=%d",(int)format_result);
    ANativeWindow_acquire((ANativeWindow*)surface);
    return 0;
}
static void host_surface_release(void*ctx,void*surface){
    (void)ctx;
    if(surface)ANativeWindow_release((ANativeWindow*)surface);
}
// #143 WSI 注入边界的真实实现：present 设备 + 自有 swapchain，clear-only FIFO。
// 缺 VK_EXT_swapchain_maintenance1 时显式 UNSUPPORTED，不回落 CPU 复制路径。
// #143 real implementation of the WSI boundary: present device plus owned FIFO clear-only swapchain. Missing
// VK_EXT_swapchain_maintenance1 is an explicit UNSUPPORTED result with no CPU-copy fallback.
static int wsi_open(void*ctx,void*window,uint32_t width,uint32_t height){
    app_state*s=ctx;
    dm_vk_present_options present=DM_VK_PRESENT_OPTIONS_INIT;present.window=window;
    VkResult r=dm_vk_create_present(&present,&s->wsi_vk);
    if(r==VK_ERROR_FEATURE_NOT_PRESENT){LOG("wsi unsupported: present init lacks a required feature (%d)",(int)r);wsi_report_fail(s,DM_WSI_STAGE_INIT,(int)r,1,1);return DM_WSI_UNSUPPORTED;}
    if(r!=VK_SUCCESS){LOG("wsi present device failed: %d",(int)r);wsi_report_fail(s,DM_WSI_STAGE_INIT,(int)r,0,1);return DM_WSI_ERROR;}
    if(!s->wsi_vk.swapchain_maintenance1){
        LOG("wsi unsupported: VK_EXT_swapchain_maintenance1 unavailable; explicit mode does not fall back");
        wsi_report_fail(s,DM_WSI_STAGE_INIT,DM_WSI_CODE_UNKNOWN,1,1);   // 能力缺失：没有 VkResult 可报，保持 unknown | capability fact, no VkResult
        dm_vk_destroy(&s->wsi_vk);s->wsi_vk=(dm_vk){0};return DM_WSI_UNSUPPORTED;
    }
    dm_vk_swapchain_options options=DM_VK_SWAPCHAIN_OPTIONS_INIT;
    options.width=width;options.height=height;
    options.clear.float32[0]=0.05f;options.clear.float32[1]=0.10f;options.clear.float32[2]=0.20f;options.clear.float32[3]=1.0f;
    r=dm_vk_swapchain_create(&s->wsi_vk,&options,&s->wsi_chain);
    if(r!=VK_SUCCESS){
        LOG("wsi swapchain create failed: %d",(int)r);
        wsi_report_fail(s,DM_WSI_STAGE_SWAPCHAIN,(int)r,0,1);
        dm_vk_destroy(&s->wsi_vk);s->wsi_vk=(dm_vk){0};return DM_WSI_ERROR;
    }
    s->wsi_chain_live=1;
    // 记录实际协商结果（请求尺寸与 extent 分开）；未知保持 unknown，不编造。| Record the real negotiation; unknown stays unknown.
    dm_wsi_report_negotiated(&s->report,s->wsi_chain.extent.width,s->wsi_chain.extent.height,
        (int)s->wsi_chain.format,(int)s->wsi_chain.present_mode,s->wsi_chain.image_count,
        s->wsi_vk.swapchain_maintenance1?1:0);
    {char line[256];dm_wsi_report_config_line(&s->report,line,sizeof line);LOG("%s",line);}
    return 0;
}
static int wsi_close(void*ctx){
    app_state*s=ctx;
    if(s->wsi_chain_live){
        VkResult r=dm_vk_swapchain_destroy(&s->wsi_vk,&s->wsi_chain);
        if(r!=VK_SUCCESS){LOG("wsi swapchain retirement not proven: %d; objects retained",(int)r);wsi_report_fail(s,DM_WSI_STAGE_DRAIN,(int)r,0,0);return 1;}
        s->wsi_chain_live=0;s->wsi_chain=(dm_vk_swapchain){0};
    }
    if(s->wsi_vk.device){dm_vk_destroy(&s->wsi_vk);s->wsi_vk=(dm_vk){0};}
    return 0;
}
static VkResult wsi_acquire(void*ctx,int*suboptimal){app_state*s=ctx;VkResult r=dm_vk_swapchain_acquire(&s->wsi_vk,&s->wsi_chain,suboptimal);
    if(r!=VK_SUCCESS&&r!=VK_SUBOPTIMAL_KHR)dm_wsi_report_note_stage(&s->report,DM_WSI_STAGE_ACQUIRE,(int)r);
    return r;}
static VkResult wsi_submit(void*ctx){app_state*s=ctx;VkResult r=dm_vk_swapchain_submit(&s->wsi_vk,&s->wsi_chain);
    if(r!=VK_SUCCESS)dm_wsi_report_note_stage(&s->report,DM_WSI_STAGE_SUBMIT,(int)r);
    return r;}
static VkResult wsi_present(void*ctx,int*suboptimal){app_state*s=ctx;VkResult r=dm_vk_swapchain_present(&s->wsi_vk,&s->wsi_chain,suboptimal);
    if(r!=VK_SUCCESS&&r!=VK_SUBOPTIMAL_KHR&&r!=VK_ERROR_OUT_OF_DATE_KHR)dm_wsi_report_note_stage(&s->report,DM_WSI_STAGE_PRESENT,(int)r);
    return r;}
static int wsi_frame(void*ctx){
    app_state*s=ctx;
    dm_wsi_frame_ops ops={.ctx=s,.acquire=wsi_acquire,.submit=wsi_submit,.present=wsi_present};
    dm_wsi_status status=dm_wsi_present_frame(&ops);
    dm_wsi_report_result(&s->report,status);
    if(status==DM_WSI_ERROR||status==DM_WSI_UNSUPPORTED){
        // 用真实阶段/原始 VkResult 记录，不从控制器枚举反推。| Real stage and raw VkResult, not inferred from the enum.
        char line[320];dm_wsi_report_failure_line(&s->report,line,sizeof line);LOG("wsi frame failed: %s",line);
        wsi_report_snapshot(s,"frame failure");
    }
    return (int)status;
}
static void lifecycle_state(void*ctx,dm_app_state state){
    (void)ctx;
    LOG("app lifecycle state=%s",dm_app_lifecycle_state_name(state));
}
// #143 桥的生命周期回调：直接调用真实控制器，测试用同一套顺序。| #143 bridge lifecycle calls into the real controller.
static int bridge_surface_created(void*ctx,void*window){return dm_app_lifecycle_surface_created((dm_app_lifecycle*)ctx,window);}
static int bridge_surface_destroyed(void*ctx,void*window){return dm_app_lifecycle_surface_destroyed((dm_app_lifecycle*)ctx,window);}
static void bridge_trace(void*ctx,const char*event){(void)ctx;LOG("wsi bridge: %s",event);}
static float activity_density(ANativeActivity*a){
    AConfiguration*config=AConfiguration_new();
    if(!config)return 1.0f;
    AConfiguration_fromAssetManager(config,a->assetManager);
    int dpi=AConfiguration_getDensity(config);
    AConfiguration_delete(config);
    return dpi>0?(float)dpi/160.0f:1.0f;
}
/* Read-only diagnostic HUD observation. Motion events only mark the coalesced
   scheduler dirty; the existing 8 ms looper timer performs the actual draw. */
static void touch_refresh_hud(app_state*s){
    if(!s||!s->arbiter||s->density<=0){if(s){s->hud_obs_valid=0;dm_touch_hud_frame_suppress(&s->hud_frame);}return;}
    dm_android_touch_observation o;
    if(!dm_input_arbiter_observe(s->arbiter,&o)){
        s->hud_obs_valid=0;dm_touch_hud_frame_suppress(&s->hud_frame);return;
    }
    if(!s->hud_obs_valid||memcmp(&s->hud_obs,&o,sizeof o)!=0){
        s->hud_obs=o;s->hud_obs_valid=1;dm_touch_hud_frame_dirty(&s->hud_frame);
    }
}
/* Production CPU overlay seam. The platform-neutral module validates, clips
   and writes only visible pixels; this wrapper only adapts the Android buffer. */
static int draw_hud_overlay(app_state*s,ANativeWindow_Buffer*buffer){
    if(!s||!s->arbiter||!buffer||!buffer->bits||!buffer->width||!buffer->height)return 0;
    if(buffer->format!=WINDOW_FORMAT_RGBA_8888||buffer->stride<(int32_t)buffer->width)return 0;
    if(!s->hud_obs_valid)return 0;
    if((uint32_t)buffer->width!=s->view.view_width_px||(uint32_t)buffer->height!=s->view.view_height_px)return 0;
    dm_touch_hud_snapshot h;
    if(!dm_touch_hud_snapshot_from_observation(&s->hud_obs,(uint32_t)buffer->width,(uint32_t)buffer->height,&h))return 0;
    uint64_t cap64=(uint64_t)(uint32_t)buffer->stride*(uint64_t)(uint32_t)buffer->height;
    if(cap64>UINT64_MAX/4u)return 0;
    cap64*=4u;
    if(cap64>(uint64_t)SIZE_MAX)return 0;
    return dm_touch_hud_overlay_draw(&h,buffer->bits,(uint32_t)buffer->width,
                                     (uint32_t)buffer->height,(uint32_t)buffer->stride,(size_t)cap64);
}
static void touch_consume(app_state*s,const char*reason){
    if(!s||!s->arbiter)return;
    touch_refresh_hud(s);
    dm_npad_state state;
    if(!dm_input_arbiter_read(s->arbiter,&state))return;
    if(state.attributes!=1)return;
    int64_t now=monotonic_ns();
    int changed=state.buttons!=s->last_touch_buttons||
        state.lx!=s->last_touch_lx||state.ly!=s->last_touch_ly||
        state.rx!=s->last_touch_rx||state.ry!=s->last_touch_ry;
    if(changed||s->last_touch_log_ns==0||
       (now>=0&&now-s->last_touch_log_ns>=250000000LL)){
        dm_touch_diag diag_copy;
        const dm_touch_diag*diag=dm_input_arbiter_diag(s->arbiter,&diag_copy)?&diag_copy:NULL;
        const char*first=diag?dm_touch_invalid_reason_name(diag->first_invalid_reason):"none";
        const char*last=diag?dm_touch_invalid_reason_name(diag->last_invalid_reason):"none";
        const uint64_t*rc=diag?diag->invalid_reason:NULL;
        LOG("touch %s buttons=%llu lx=%d ly=%d rx=%d ry=%d diag_reads=%llu invalid=%llu rejected=%llu drained=%llu up_retries=%llu cancel_fallbacks=%llu invalid_first=%s invalid_last=%s reasons[time=%llu action=%llu count=%llu index=%llu id=%llu coord=%llu dup=%llu unknown=%llu owned=%llu capacity=%llu]",
            reason,(unsigned long long)state.buttons,state.lx,state.ly,state.rx,state.ry,
            (unsigned long long)(diag?diag->diagnostic_reads:0),
            (unsigned long long)(diag?diag->invalid_events:0),
            (unsigned long long)(diag?diag->rejected_events:0),
            (unsigned long long)(diag?diag->drained_pulses:0),
            (unsigned long long)(diag?diag->up_retries:0),
            (unsigned long long)(diag?diag->cancel_fallbacks:0),first,last,
            (unsigned long long)(rc?rc[DM_TOUCH_INVALID_TIME_NEGATIVE]:0),
            (unsigned long long)(rc?rc[DM_TOUCH_INVALID_ACTION_RANGE]:0),
            (unsigned long long)(rc?rc[DM_TOUCH_INVALID_COUNT_RANGE]:0),
            (unsigned long long)(rc?rc[DM_TOUCH_INVALID_ACTION_INDEX]:0),
            (unsigned long long)(rc?rc[DM_TOUCH_INVALID_POINT_ID_NEGATIVE]:0),
            (unsigned long long)(rc?rc[DM_TOUCH_INVALID_COORDINATE_NONFINITE]:0),
            (unsigned long long)(rc?rc[DM_TOUCH_INVALID_DUPLICATE_ID]:0),
            (unsigned long long)(rc?rc[DM_TOUCH_INVALID_UNKNOWN_POINTER]:0),
            (unsigned long long)(rc?rc[DM_TOUCH_INVALID_POINTER_ALREADY_OWNED]:0),
            (unsigned long long)(rc?rc[DM_TOUCH_INVALID_POINTER_CAPACITY]:0));
        s->last_touch_log_ns=now;
        s->last_touch_buttons=state.buttons;
        s->last_touch_lx=state.lx;s->last_touch_ly=state.ly;
        s->last_touch_rx=state.rx;s->last_touch_ry=state.ry;
    }
}
/* A lifecycle reset is the only admission reset. There is no platform
   hotplug callback in this slice, so a device does not silently replace an
   already admitted one. Clear the caller-owned converter and its prior sample
   together with the arbiter's gamepad contribution. */
static void gamepad_rearm(app_state*s){
    if(!s)return;
    if(s->gamepad_admitted&&s->arbiter)
        (void)dm_input_arbiter_disconnect(s->arbiter,s->gamepad_id,s->gamepad_generation);
    s->gamepad_admitted=0;
    dm_gamepad_context_init(&s->gamepad_context);
    memset(&s->gamepad_state,0,sizeof(s->gamepad_state));
    int64_t now=monotonic_ns();
    s->gamepad_after_ns=now>0?now:0;
}
static int controller_hotplug_acquire_owner(app_state*s){
    if(!s||s->controller_session)return 0;
    if(g_controller_app&&g_controller_app!=s)return 0;
    g_controller_app=s;
    return 1;
}
static void controller_hotplug_release_owner(app_state*s){
    if(s&&g_controller_app==s)g_controller_app=NULL;
}
static void controller_hotplug_dispatch(app_state*s,uint64_t session,int event,int device_id){
    if(!s||!s->controller_session||session!=s->controller_session)return;
    if(event!=dm_controller_removed&&event!=dm_controller_changed)return;
    if(!s->gamepad_admitted||device_id<0||(uint32_t)device_id!=s->gamepad_id)return;
    gamepad_rearm(s);
    touch_consume(s,event==dm_controller_removed?"controller_removed":"controller_changed");
}
static void touch_cancel(app_state*s,const char*reason,int64_t now){
    if(!s||!s->arbiter)return;
    (void)now;
    if(!dm_input_arbiter_queue_changed(s->arbiter))
        LOG("touch cancel rejected: %s",reason);
    gamepad_rearm(s);
    touch_consume(s,reason);
}
static uint32_t gamepad_next_generation(app_state*s){
    s->gamepad_generation++;
    if(!s->gamepad_generation)s->gamepad_generation=1;
    return s->gamepad_generation;
}
static int gamepad_source(int32_t source){
    return dm_gamepad_source_is_gamepad(source);
}
static void gamepad_sample_from_state(const dm_npad_state*state,dm_pad_sample*sample){
    memset(sample,0,sizeof(*sample));
    sample->buttons=state->buttons;
    sample->lx=state->lx;sample->ly=state->ly;
    sample->rx=state->rx;sample->ry=state->ry;
    sample->connected=1;
}
static void touch_resize_window(app_state*s,ANativeWindow*window){
    if(!s||!s->arbiter||!window)return;
    int width=ANativeWindow_getWidth(window),height=ANativeWindow_getHeight(window);
    if(width<=0||height<=0||s->density<=0)return;
    if(!dm_view_policy_set_view(&s->view,(uint32_t)width,(uint32_t)height,s->density))return;
    float width_pt=s->view.view_width_pt,height_pt=s->view.view_height_pt;
    if(width_pt==s->touch_width_pt&&height_pt==s->touch_height_pt&&s->density==s->touch_density)return;
    int64_t now=monotonic_ns();if(now<0)now=0;
    if(!dm_input_arbiter_set_geometry(s->arbiter,width_pt,height_pt,s->density,now)){
        LOG("touch resize rejected: %.1fx%.1f pt",width_pt,height_pt);return;
    }
    s->touch_width_pt=width_pt;s->touch_height_pt=height_pt;s->touch_density=s->density;
    gamepad_rearm(s);
    touch_consume(s,"resize");
}
// 诊断读回目标尺寸；窗口缓冲尺寸与它无关（居中裁剪由 app/present_policy.c 决定）。
// Diagnostic readback extent; the window buffer size is independent and handled by the centred crop in app/present_policy.c.
#define DM_DIAG_TARGET_EXTENT 256u
static void draw_diagnostic(ANativeActivity*a,ANativeWindow*window,int external_redraw){
    app_state*s=a->instance;if(!s||!window)return;
    // #143 WSI 模式下禁止任何 CPU 锁窗绘制入口，包括触摸 HUD 定时器触发的这条路径。
    // #143 no CPU window-lock render entry may run in WSI mode, including this touch-HUD timer path.
    if(s->wsi.enabled){LOG("cpu-copy renderer disabled in wsi mode; draw skipped");return;}
    touch_refresh_hud(s);
    if(!dm_diag_scene_ready(s->lifecycle,window)){
        LOG("graphics draw suppressed: state=%s owned-surface=%s",
            dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)),
            dm_app_lifecycle_surface(s->lifecycle)?"yes":"no");
        return;
    }
    if(external_redraw&&s->hud_obs_valid)dm_touch_hud_frame_dirty(&s->hud_frame);
    if(!s->vertex[0]||!s->fragment[0]){LOG("graphics draw skipped: no staged shader assets");return;}
    if(!s->vk.device){VkResult created=dm_vk_create(&s->vk);if(created!=VK_SUCCESS){LOG("Vulkan create failed: %d",created);return;}}
    const size_t target_bytes=(size_t)DM_DIAG_TARGET_EXTENT*DM_DIAG_TARGET_EXTENT*4u;
    uint8_t*pixels=malloc(target_bytes);
    if(!pixels){LOG("graphics target allocation failed: %zu bytes",target_bytes);return;}
    dm_vk_pixels target={.width=DM_DIAG_TARGET_EXTENT,.height=DM_DIAG_TARGET_EXTENT,.bytes=pixels,.capacity=target_bytes};
    const dm_diag_scene*scene=dm_diag_scene_owned();
    // 场景顶点复制进固定 16 字节布局；纹理与索引是借用输入，API 同步复制，不保留指针。
    // Scene vertices are copied into the fixed 16-byte layout; texture and indices are borrowed and copied synchronously by the API.
    dm_vk_indexed_textured_vertex vertices[DM_DIAG_SCENE_VERTEX_COUNT];
    for(uint32_t i=0;i<DM_DIAG_SCENE_VERTEX_COUNT;i++){
        vertices[i].position[0]=scene->vertices[i].position[0];
        vertices[i].position[1]=scene->vertices[i].position[1];
        vertices[i].uv[0]=scene->vertices[i].uv[0];
        vertices[i].uv[1]=scene->vertices[i].uv[1];
    }
    dm_vk_indexed_textured_draw request={.vertices=vertices,.vertex_count=scene->vertex_count,
        .vertex_stride=DM_VK_INDEXED_TEXTURED_VERTEX_STRIDE,.indices=scene->indices,.index_count=scene->index_count,
        .texture_pixels=scene->texture,.texture_size=scene->texture_size,
        .texture_width=scene->texture_width,.texture_height=scene->texture_height,
        .vertex_spv=s->vertex,.fragment_spv=s->fragment};
    VkResult r=dm_vk_draw_indexed_textured_offscreen(&s->vk,&request,&target);
    if(r!=VK_SUCCESS){LOG("indexed textured draw failed: %d",r);free(pixels);return;}
    dm_diag_scene_mismatch mismatch;
    uint32_t probes=dm_diag_scene_check_pixels(scene,pixels,target.width,target.height,target.capacity,&mismatch);
    if(!probes){
        if(mismatch.kind==DM_DIAG_SCENE_MISMATCH_PIXEL)
            LOG("indexed textured pixel-check=FAIL at %u,%u expected=%u,%u,%u,%u observed=%u,%u,%u,%u",
                mismatch.x,mismatch.y,mismatch.expected[0],mismatch.expected[1],mismatch.expected[2],mismatch.expected[3],
                mismatch.observed[0],mismatch.observed[1],mismatch.observed[2],mismatch.observed[3]);
        else LOG("indexed textured pixel-check=FAIL: invalid readback canvas");
        free(pixels);return;
    }
    int window_width=ANativeWindow_getWidth(window),window_height=ANativeWindow_getHeight(window);
    if(window_width>0&&window_height>0)
        (void)dm_view_policy_set_view(&s->view,(uint32_t)window_width,(uint32_t)window_height,s->density);
    ANativeWindow_Buffer buffer;
    if(ANativeWindow_lock(window,&buffer,NULL)){LOG("window lock failed");free(pixels);return;}
    dm_present_source source={.pixels=pixels,.width=target.width,.height=target.height,.capacity=target.capacity};
    dm_present_target present={.bits=buffer.bits,.width=buffer.width>0?(uint32_t)buffer.width:0,
        .height=buffer.height>0?(uint32_t)buffer.height:0,
        .stride=buffer.stride>0?(uint32_t)buffer.stride:0,
        .format_rgba8888=buffer.format==WINDOW_FORMAT_RGBA_8888};
    dm_present_rect copied;
    int presented=dm_present_copy(&source,&present,&copied);
    int frame_open=0;
    int overlaid=0;
    if(presented&&s->hud_obs_valid){
        if(buffer.format!=WINDOW_FORMAT_RGBA_8888||buffer.stride<(int32_t)buffer.width||
           (uint32_t)buffer.width!=s->view.view_width_px||(uint32_t)buffer.height!=s->view.view_height_px){
            dm_touch_hud_frame_suppress(&s->hud_frame);
        }else{
            frame_open=dm_touch_hud_frame_begin(&s->hud_frame,external_redraw,!external_redraw,1,1);
            if(frame_open){
                overlaid=draw_hud_overlay(s,&buffer);
                dm_touch_hud_frame_composed(&s->hud_frame,overlaid);
            }
        }
    }
    if(!presented){
        LOG("window copy rejected: format=%d %dx%d stride=%d; readback kept out of the window",
            (int)buffer.format,(int)buffer.width,(int)buffer.height,(int)buffer.stride);
        dm_touch_hud_frame_suppress(&s->hud_frame);
    }
    else (void)dm_view_policy_set_buffer(&s->view,present.width,present.height);
    int post_failed=ANativeWindow_unlockAndPost(window);
    if(post_failed)LOG("window post failed");
    if(frame_open){
        int ack=dm_touch_hud_frame_complete(&s->hud_frame,post_failed==0&&presented);
        if(!ack)LOG("touch HUD frame not acknowledged; retry pending");
    }
    if(!post_failed&&presented){
        LOG("indexed textured diagnostic: %ux%u pixel-check=PASS (%u probes), copy %ux%u at %u,%u into %dx%d view / %dx%d buffer; hud=%s; game presenter not implemented",
        target.width,target.height,probes,copied.width,copied.height,copied.x,copied.y,
        window_width,window_height,(int)buffer.width,(int)buffer.height,overlaid?"geometry":"unavailable");}
    free(pixels);
}
static void stop_input_timer(app_state*s){
    if(!s||s->timer_fd<0)return;
    if(s->looper)ALooper_removeFd(s->looper,s->timer_fd);
    close(s->timer_fd);s->timer_fd=-1;
}
static int input_timer(int fd,int events,void*data){
    app_state*s=data;
    if(events&(ALOOPER_EVENT_ERROR|ALOOPER_EVENT_HANGUP)){
        LOG("touch timer error/hangup events=%d",events);
        stop_input_timer(s);
        dm_input_policy_set_timer(&s->gate,0);s->touch_allowed=0;
        int64_t now=monotonic_ns();touch_cancel(s,"timer_error",now<0?0:now);
        return 0;
    }
    uint64_t expirations;
    ssize_t got=read(fd,&expirations,sizeof(expirations));
    if(got<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR){
        LOG("touch timer read failed: %d",errno);
        stop_input_timer(s);
        dm_input_policy_set_timer(&s->gate,0);s->touch_allowed=0;
        int64_t now=monotonic_ns();touch_cancel(s,"timer_read_error",now<0?0:now);
        return 0;
    }
    if(!s->touch_allowed)return 1;
    int64_t now=monotonic_ns();if(now<0)return 1;
    int ticked=dm_input_arbiter_tick(s->arbiter,now);
    if(ticked)touch_consume(s,"timer");
    if(dm_touch_hud_frame_timer_ready(&s->hud_frame,s->window!=NULL&&s->touch_allowed))
        draw_diagnostic(s->activity,s->window,0);
    return 1;
}
static int start_input_timer(app_state*s){
    if(!s||!s->arbiter)return 0;
    if(s->timer_fd>=0)return 1;
    int fd=timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK|TFD_CLOEXEC);
    if(fd<0){LOG("timerfd_create failed: %d",errno);return 0;}
    struct itimerspec spec;
    memset(&spec,0,sizeof(spec));
    spec.it_interval.tv_nsec=8*1000*1000;
    spec.it_value=spec.it_interval;
    if(timerfd_settime(fd,0,&spec,NULL)){
        LOG("timerfd_settime failed: %d",errno);close(fd);return 0;
    }
    ALooper*looper=ALooper_forThread();
    if(!looper){LOG("timer looper unavailable");close(fd);return 0;}
    if(ALooper_addFd(looper,fd,2,ALOOPER_EVENT_INPUT,input_timer,s)<0){
        LOG("timer looper add failed");close(fd);return 0;
    }
    s->timer_fd=fd;
    return 1;
}
// WSI 连续帧定时器：与输入定时器相互独立，不经过 input_policy，因此不依赖输入队列。
// WSI continuous-frame timer, independent of the input timer and of input_policy, so no input queue is required.
static void stop_frame_timer(app_state*s){
    if(!s||s->frame_fd<0)return;
    if(s->looper)ALooper_removeFd(s->looper,s->frame_fd);
    close(s->frame_fd);s->frame_fd=-1;
}
static int frame_timer(int fd,int events,void*data){
    app_state*s=data;
    if(events&(ALOOPER_EVENT_ERROR|ALOOPER_EVENT_HANGUP)){
        LOG("wsi frame timer error/hangup events=%d; frames stop",events);
        wsi_report_fail(s,DM_WSI_STAGE_TIMER,DM_WSI_CODE_UNKNOWN,0,0);   // 事件位不是 errno：原始码未知 | the event mask is not errno
        wsi_report_snapshot(s,"frame timer error");
        stop_frame_timer(s);return 0;
    }
    uint64_t expirations;
    ssize_t got=read(fd,&expirations,sizeof(expirations));
    if(got<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR){
        LOG("wsi frame timer read failed: %d; frames stop",errno);
        wsi_report_fail(s,DM_WSI_STAGE_TIMER,errno,0,0);
        wsi_report_snapshot(s,"frame timer read failed");
        stop_frame_timer(s);return 0;
    }
    dm_wsi_timer_action action=DM_WSI_TIMER_STOP;
    dm_wsi_status status=dm_wsi_bridge_dispatch(&s->wsi_bridge,s->resumed,&action);
    if(action==DM_WSI_TIMER_STOP){
        // 每个 tick 的结果都在 dispatch 里判定：终态/等待/无效状态停表，不留持续唤醒。
        // Every tick result is decided in dispatch: terminal/awaiting/ineligible states stop the timer, leaving no wakeups.
        LOG("wsi frames stop: status=%d frames=%u losses=%u",(int)status,s->wsi.frames,s->wsi.losses);
        stop_frame_timer(s);
        if(status==DM_WSI_ERROR||status==DM_WSI_UNSUPPORTED||status==DM_WSI_SURFACE_LOST)
            wsi_report_snapshot(s,"frame dispatch terminal");
    } else {
        wsi_report_progress(s);
    }
    if((int)status!=s->wsi_last_logged){
        s->wsi_last_logged=(int)status;
        if(status==DM_WSI_IDLE)LOG("wsi idle: paused/windowless/failed; no device work and no spin");
        else LOG("wsi status=%d frames=%u rebuilds=%u losses=%u",(int)status,s->wsi.frames,s->wsi.rebuilds,s->wsi.losses);
    }
    return 1;
}
static int start_frame_timer(app_state*s){
    if(!s)return 0;
    if(s->frame_fd>=0)return 1;
    int fd=timerfd_create(CLOCK_MONOTONIC,TFD_NONBLOCK|TFD_CLOEXEC);
    if(fd<0){LOG("wsi frame timerfd_create failed: %d",errno);return 0;}
    struct itimerspec spec;memset(&spec,0,sizeof spec);
    spec.it_interval.tv_nsec=16*1000*1000;spec.it_value=spec.it_interval;
    if(timerfd_settime(fd,0,&spec,NULL)){LOG("wsi frame timerfd_settime failed: %d",errno);close(fd);return 0;}
    ALooper*looper=s->looper?s->looper:ALooper_forThread();
    if(!looper){LOG("wsi frame timer looper unavailable");close(fd);return 0;}
    if(ALooper_addFd(looper,fd,3,ALOOPER_EVENT_INPUT,frame_timer,s)<0){LOG("wsi frame timer looper add failed");close(fd);return 0;}
    s->frame_fd=fd;
    return 1;
}
static void update_touch_state(app_state*s){
    if(!s)return;
    dm_input_policy_set_resumed(&s->gate,s->resumed);
    dm_input_policy_set_focused(&s->gate,s->focused);
    dm_input_policy_set_queue(&s->gate,s->input!=NULL);
    dm_input_policy_set_window(&s->gate,s->window!=NULL);
    dm_input_policy_set_touch(&s->gate,s->arbiter!=NULL);
    if(!dm_input_policy_ready(&s->gate)){
        stop_input_timer(s);dm_input_policy_set_timer(&s->gate,0);s->touch_allowed=0;return;
    }
    if(!start_input_timer(s)){
        dm_input_policy_set_timer(&s->gate,0);s->touch_allowed=0;
        s->timer_fd=-1;
        LOG("touch unavailable: timer start failed; gestures gated until retry");
        int64_t now=monotonic_ns();touch_cancel(s,"timer_unavailable",now<0?0:now);
        return;
    }
    dm_input_policy_set_timer(&s->gate,1);
    s->touch_allowed=1;
}
static int motion_action(int32_t action){
    switch(action&AMOTION_EVENT_ACTION_MASK){
    case AMOTION_EVENT_ACTION_DOWN:return DM_TOUCH_ACTION_DOWN;
    case AMOTION_EVENT_ACTION_MOVE:return DM_TOUCH_ACTION_MOVE;
    case AMOTION_EVENT_ACTION_UP:return DM_TOUCH_ACTION_UP;
    case AMOTION_EVENT_ACTION_CANCEL:return DM_TOUCH_ACTION_CANCEL;
    case AMOTION_EVENT_ACTION_POINTER_DOWN:return DM_TOUCH_ACTION_POINTER_DOWN;
    case AMOTION_EVENT_ACTION_POINTER_UP:return DM_TOUCH_ACTION_POINTER_UP;
    default:return -1;
    }
}
static void handle_motion(app_state*s,AInputEvent*event){
    if(!s||!s->arbiter)return;
    if(!s->touch_allowed){LOG("touch event ignored while inactive");return;}
    int32_t packed=AMotionEvent_getAction(event);
    int action=motion_action(packed);
    if(action<0){LOG("touch unsupported action=%d",packed&AMOTION_EVENT_ACTION_MASK);return;}
    uint32_t count=(uint32_t)AMotionEvent_getPointerCount(event);
    dm_touch_event translated;
    memset(&translated,0,sizeof(translated));
    translated.action=(dm_touch_action)action;
    translated.action_index=(packed&AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)>>AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;
    translated.time_ns=AMotionEvent_getEventTime(event);
    if(count>DM_TOUCH_MAX_POINTS){
        LOG("touch pointer count %u exceeds bridge bound",count);
        return;
    }
    translated.pointer_count=count;
    for(uint32_t i=0;i<count;i++){
        translated.points[i].id=AMotionEvent_getPointerId(event,(size_t)i);
        translated.points[i].x_px=AMotionEvent_getX(event,(size_t)i);
        translated.points[i].y_px=AMotionEvent_getY(event,(size_t)i);
    }
    dm_input_source before=dm_input_arbiter_source(s->arbiter);
    if(!dm_input_arbiter_touch(s->arbiter,&translated))
        LOG("touch event rejected action=%d count=%u",action,count);
    else if(before==DM_INPUT_SOURCE_GAMEPAD&&
            dm_input_arbiter_source(s->arbiter)==DM_INPUT_SOURCE_TOUCH){
        /* A valid touch takeover retires the old controller contribution and
           prior sample. Admission/id/generation stay intact, but a neutral
           MotionEvent cannot republish a canceled button. A rejected touch
           event never reaches this branch. */
        if(s->gamepad_admitted){
            dm_gamepad_context_init(&s->gamepad_context);
            memset(&s->gamepad_state,0,sizeof(s->gamepad_state));
        }
    }
    touch_consume(s,"event");
}
/* Android getters report device IDs as non-negative int32 values. The arbiter
   stores one caller-owned scalar id; real IDs are not a 0..15 table. Reject
   the getter's negative sentinel before the uint32 API boundary. */
static void handle_gamepad(app_state*s,AInputEvent*event){
    if(!s||!s->arbiter)return;
    if(!s->touch_allowed){LOG("gamepad event ignored while inactive");return;}
    int32_t raw_id=AInputEvent_getDeviceId(event);
    if(raw_id<0){LOG("gamepad event rejected: negative device id");return;}
    uint32_t id=(uint32_t)raw_id;
    int32_t type=AInputEvent_getType(event);
    int64_t event_time;
    if(type==AINPUT_EVENT_TYPE_KEY)event_time=AKeyEvent_getEventTime(event);
    else if(type==AINPUT_EVENT_TYPE_MOTION)event_time=AMotionEvent_getEventTime(event);
    else return;
    if(event_time<0||event_time<s->gamepad_after_ns){
        LOG("gamepad event rejected: stale time");
        return;
    }
    if(s->gamepad_admitted&&id!=s->gamepad_id){
        LOG("gamepad event rejected: another device while one is admitted");
        return;
    }
    dm_gamepad_context next_context=s->gamepad_context;
    dm_npad_state next_state=s->gamepad_state;
    if(!dm_gamepad_event_apply(&next_context,event,&next_state))return;
    dm_pad_sample sample;
    gamepad_sample_from_state(&next_state,&sample);
    if(!s->gamepad_admitted){
        uint32_t generation=gamepad_next_generation(s);
        if(!dm_input_arbiter_connect(s->arbiter,id,generation))return;
        if(!dm_input_arbiter_gamepad(s->arbiter,id,generation,&sample)){
            (void)dm_input_arbiter_disconnect(s->arbiter,id,generation);
            return;
        }
        s->gamepad_id=id;s->gamepad_generation=generation;s->gamepad_admitted=1;
    }else if(!dm_input_arbiter_gamepad(s->arbiter,id,s->gamepad_generation,&sample)){
        return;
    }
    s->gamepad_context=next_context;
    s->gamepad_state=next_state;
    touch_consume(s,"gamepad");
}
static int input_ready(int fd,int events,void*data){
    (void)fd;(void)events;ANativeActivity*a=data;app_state*s=a->instance;if(!s||!s->input)return 0;
    AInputEvent*event;while(AInputQueue_getEvent(s->input,&event)>=0){
        if(AInputQueue_preDispatchEvent(s->input,event))continue;int handled=0;
        if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_KEY&&AKeyEvent_getKeyCode(event)==AKEYCODE_BACK){
            if(AKeyEvent_getAction(event)==AKEY_EVENT_ACTION_UP)ANativeActivity_finish(a);handled=1;
        }else if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_MOTION&&
                 (AInputEvent_getSource(event)&AINPUT_SOURCE_TOUCHSCREEN)==AINPUT_SOURCE_TOUCHSCREEN){
            handle_motion(s,event);handled=1;
        }else if(gamepad_source(AInputEvent_getSource(event))){
            handle_gamepad(s,event);handled=1;
        }
        AInputQueue_finishEvent(s->input,event,handled);
    }return 1;
}
static void input_created(ANativeActivity*a,AInputQueue*queue){app_state*s=a->instance;if(!s)return;
    if(s->input==queue){update_touch_state(s);return;}
    if(s->input){
        AInputQueue_detachLooper(s->input);s->input=NULL;update_touch_state(s);
        if(s->arbiter&&!dm_input_arbiter_queue_changed(s->arbiter))
            LOG("arbiter queue-change invalidation rejected");
        gamepad_rearm(s);
        LOG("input queue replaced; arbiter retained");
    }
    if(!s->arbiter){
        s->density=activity_density(a);
        s->arbiter=dm_input_arbiter_create(667,375,s->density);
        if(!s->arbiter){LOG("input arbiter create failed");return;}
        LOG("touch game provider unavailable: production scene/hit/action disabled");
        s->touch_width_pt=667;s->touch_height_pt=375;s->touch_density=s->density;
        if(s->window)touch_resize_window(s,s->window);
    }
    ALooper*looper=ALooper_forThread();if(!looper)looper=ALooper_prepare(0);
    if(!looper){LOG("input looper unavailable");return;}
    s->looper=looper;
    s->input=queue;AInputQueue_attachLooper(queue,looper,1,input_ready,a);
    update_touch_state(s);
}
static void input_destroyed(ANativeActivity*a,AInputQueue*queue){app_state*s=a->instance;if(!s)return;
    if(s->input!=queue){LOG("stale input queue destroy ignored");return;}
    AInputQueue_detachLooper(queue);s->input=NULL;update_touch_state(s);
    if(s->arbiter&&!dm_input_arbiter_queue_changed(s->arbiter))
        LOG("arbiter queue-loss invalidation rejected");
    gamepad_rearm(s);
    touch_consume(s,"queue_lost");
}
static void window_ready(ANativeActivity*a,ANativeWindow*window){
    app_state*s=a->instance;if(!s||!s->lifecycle)return;
    if(s->wsi.enabled){
        int width=ANativeWindow_getWidth(window),height=ANativeWindow_getHeight(window);
        if(width<=0||height<=0){LOG("wsi window has no extent; awaiting a valid replacement");return;}
        // 桥内固定顺序：旧窗口先排空 → 生命周期取新引用 → 打开 WSI；失败不绘制、不回落、不释放引用。
        // Bridge order: drain the old window first, then acquire the new lifecycle reference, then open WSI.
        int ready=dm_wsi_bridge_window_ready(&s->wsi_bridge,window,(uint32_t)width,(uint32_t)height);
        // Replacement can acquire the new reference before WSI open fails. Never retain the released old pointer.
        s->window=dm_app_lifecycle_surface(s->lifecycle);
        if(!ready){
            // 同窗口尺寸变化返回 REBUILD：这是合法的重建，不是打开失败，也不停表。
            // A same-window dimension change returns REBUILD: a legitimate rebuild, not an open failure, and the timer
            // keeps running.
            if(s->wsi.last==DM_WSI_REBUILD){
                wsi_report_progress(s);
            } else {
                stop_frame_timer(s);
                // 失败已由 wsi_open 记录时此处不再重复；生命周期拒绝（从未进入真实打开）不计为打开失败。
                // A failure already recorded inside wsi_open is not repeated; a lifecycle rejection that never entered
                // a real open is not an open failure.
                wsi_report_fail(s,DM_WSI_STAGE_OPEN,DM_WSI_CODE_UNKNOWN,s->wsi.error==(int)DM_WSI_UNSUPPORTED,s->report.attempt_open);
                wsi_report_snapshot(s,"window not ready");
                if(s->window)touch_resize_window(s,s->window);
                update_touch_state(s);
                LOG("wsi window not ready: state=%s; no cpu fallback",
                    dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)));
                return;
            }
        }
        s->window=window;
        touch_resize_window(s,window);
        update_touch_state(s);
        // 唯一调度入口：未恢复/暂停/等待替换/失败都不出帧且停表。
        // The single scheduling entry: not-resumed, paused, awaiting a surface or failed states produce no frames and stop the timer.
        dm_wsi_timer_action action=DM_WSI_TIMER_STOP;
        dm_wsi_status status=dm_wsi_bridge_dispatch(&s->wsi_bridge,s->resumed,&action);
        if(status==DM_WSI_ERROR||status==DM_WSI_UNSUPPORTED)LOG("wsi not schedulable: status=%d; frames stop",(int)status);
        if(action==DM_WSI_TIMER_STOP){
            stop_frame_timer(s);
            if(status==DM_WSI_ERROR||status==DM_WSI_UNSUPPORTED||status==DM_WSI_SURFACE_LOST)
                wsi_report_snapshot(s,"window not schedulable");
            return;
        }
        if(s->frame_fd<0&&!start_frame_timer(s)){LOG("wsi frames unavailable: frame timer start failed; continuous frames unavailable");wsi_report_fail(s,DM_WSI_STAGE_TIMER,DM_WSI_CODE_UNKNOWN,0,0);wsi_report_snapshot(s,"frame timer start failed");return;}
        wsi_report_progress(s);
        return;
    }
    if(!dm_app_lifecycle_surface_created(s->lifecycle,window)){
        LOG("surface callback rejected: state=%s pending=%u",
            dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)),
            dm_app_lifecycle_pending_work(s->lifecycle));
        return;
    }
    s->window=window;
    touch_resize_window(s,window);
    update_touch_state(s);
    if(dm_diag_scene_ready(s->lifecycle,window))draw_diagnostic(a,window,1);
}
static void window_resized(ANativeActivity*a,ANativeWindow*window){
    app_state*s=a->instance;if(!s||!window)return;
    if(!s->wsi.enabled){window_ready(a,window);return;}
    // 身份校验先于任何副作用（含触摸视图与定时器）：陈旧窗口的 resize 不得影响当前渲染器。
    // Identity first, before any side effect (touch view and timer included): a stale resize must not touch the current renderer.
    if(!dm_wsi_bridge_owns_window(&s->wsi_bridge,window)){LOG("wsi stale resize ignored");return;}
    touch_resize_window(s,window);
    int width=ANativeWindow_getWidth(window),height=ANativeWindow_getHeight(window);
    if(width<=0||height<=0){LOG("wsi resize: window has no extent");return;}
    dm_wsi_status resized=dm_wsi_bridge_window_resized(&s->wsi_bridge,window,(uint32_t)width,(uint32_t)height);
    dm_wsi_timer_action action=DM_WSI_TIMER_STOP;
    dm_wsi_status status=resized;
    if(resized!=DM_WSI_ERROR)status=dm_wsi_bridge_dispatch(&s->wsi_bridge,s->resumed,&action);
    if(resized==DM_WSI_ERROR||status==DM_WSI_ERROR||status==DM_WSI_UNSUPPORTED){
        LOG("wsi resize/rebuild failed: status=%d; frames stop",(int)status);
        wsi_report_fail(s,DM_WSI_STAGE_UNKNOWN,DM_WSI_CODE_UNKNOWN,status==DM_WSI_UNSUPPORTED,0);
        action=DM_WSI_TIMER_STOP;
    }
    if(action==DM_WSI_TIMER_STOP){stop_frame_timer(s);wsi_report_snapshot(s,"resize terminal");return;}
    if(s->frame_fd<0&&!start_frame_timer(s)){LOG("wsi frames unavailable after resize: timer start failed; continuous frames unavailable");wsi_report_fail(s,DM_WSI_STAGE_TIMER,DM_WSI_CODE_UNKNOWN,0,0);wsi_report_snapshot(s,"resize timer start failed");return;}
    wsi_report_progress(s);
}
static void window_destroyed(ANativeActivity*a,ANativeWindow*window){
    app_state*s=a->instance;if(!s||!s->lifecycle)return;
    if(s->wsi.enabled){
        // 身份校验先于停表等一切副作用。| Identity check before every side effect, the timer included.
        if(!dm_wsi_bridge_owns_window(&s->wsi_bridge,window)){LOG("wsi stale window destroy ignored");return;}
        stop_frame_timer(s);
        // 只有排空被证明成功后才让生命周期释放真实 ANativeWindow 引用。
        // The lifecycle releases the real ANativeWindow reference only after a proven drain.
        if(!dm_wsi_bridge_window_destroyed(&s->wsi_bridge,window)){
            LOG("wsi window destroy: drain not proven; ANativeWindow reference retained; no cpu fallback");
            wsi_report_snapshot(s,"window destroy drain failed");
            return;
        }
        // 窗口丢失是终态：恰好一次快照。| Window loss is terminal: exactly one snapshot.
        dm_wsi_report_mark_terminal(&s->report);
        wsi_report_snapshot(s,"window destroyed");
        s->window=NULL;update_touch_state(s);
        s->hud_obs_valid=0;dm_touch_hud_frame_suppress(&s->hud_frame);
        int64_t now=monotonic_ns();touch_cancel(s,"window_destroyed",now<0?0:now);
        return;
    }
    if(s->window==window){s->window=NULL;update_touch_state(s);
        s->hud_obs_valid=0;dm_touch_hud_frame_suppress(&s->hud_frame);
        int64_t now=monotonic_ns();touch_cancel(s,"window_destroyed",now<0?0:now);}
    if(!dm_app_lifecycle_surface_destroyed(s->lifecycle,window))
        LOG("stale surface destroy ignored");
}
static void controller_hotplug_stop(app_state*s);
static void destroy(ANativeActivity*a){app_state*s=a->instance;if(s){
    stop_input_timer(s);stop_frame_timer(s);
    if(s->wsi.enabled){
        dm_wsi_report_event(&s->report,DM_WSI_EVENT_STOP);
        if(dm_wsi_stop(&s->wsi)!=DM_WSI_OK){
            LOG("wsi teardown failed; app state and presentation ownership stay reachable (no forced device loss)");
            wsi_report_fail(s,DM_WSI_STAGE_DRAIN,DM_WSI_CODE_UNKNOWN,0,0);
            wsi_report_snapshot(s,"teardown failed");
            return;
        }
        wsi_report_snapshot(s,"destroy");
    }
    if(s->input){AInputQueue_detachLooper(s->input);s->input=NULL;}
    controller_hotplug_stop(s);
    gamepad_rearm(s);
    if(s->arbiter){dm_npad_state neutral;dm_input_arbiter_destroy(s->arbiter,&neutral);s->arbiter=NULL;
        LOG("input arbiter destroyed buttons=%llu",(unsigned long long)neutral.buttons);}
    if(s->lifecycle){
        dm_app_lifecycle_stop(s->lifecycle);
        if(!dm_app_lifecycle_destroy(s->lifecycle)){
            LOG("lifecycle destroy deferred: state=%s pending=%u",
                dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)),
                dm_app_lifecycle_pending_work(s->lifecycle));
            return; /* keep controller and app state alive; no borrowed Surface release */
        }
    } else if(s->vk.device)dm_vk_destroy(&s->vk);
    free(s);a->instance=NULL;
}}
static void low_memory(ANativeActivity*a){app_state*s=a->instance;if(s&&s->vk.device){dm_vk_destroy(&s->vk);LOG("low memory: GPU context released; redraw recreates diagnostic");}}
static void pause_app(ANativeActivity*a){app_state*s=a->instance;if(!s||!s->lifecycle)return;
    s->resumed=0;update_touch_state(s);
    if(s->arbiter&&!dm_input_arbiter_pause(s->arbiter,1))LOG("arbiter pause invalidation rejected");
    gamepad_rearm(s);
    if(s->wsi.enabled){dm_wsi_pause(&s->wsi);stop_frame_timer(s);
        dm_wsi_report_event(&s->report,DM_WSI_EVENT_PAUSE);wsi_report_snapshot(s,"pause");}
    touch_consume(s,"pause");
    if(!dm_app_lifecycle_pause(s->lifecycle))LOG("pause rejected: state=%s",dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)));}
static void resume_app(ANativeActivity*a){app_state*s=a->instance;if(!s||!s->lifecycle)return;
    if(!dm_app_lifecycle_resume(s->lifecycle)){LOG("resume rejected: state=%s",dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)));return;}
    s->resumed=1;
    if(s->arbiter){dm_input_arbiter_pause(s->arbiter,0);dm_input_arbiter_resume(s->arbiter);}
    update_touch_state(s);
    if(s->wsi.enabled){
        dm_wsi_resume(&s->wsi);
        dm_wsi_report_event(&s->report,DM_WSI_EVENT_RESUME);
        dm_wsi_timer_action action=DM_WSI_TIMER_STOP;
        dm_wsi_status status=dm_wsi_bridge_dispatch(&s->wsi_bridge,s->resumed,&action);
        if(status==DM_WSI_ERROR||status==DM_WSI_UNSUPPORTED)LOG("wsi resume not schedulable: status=%d",(int)status);
        if(action==DM_WSI_TIMER_STOP){stop_frame_timer(s);wsi_report_snapshot(s,"resume terminal");}
        else if(s->frame_fd<0&&!start_frame_timer(s)){LOG("wsi frames unavailable after resume: timer start failed; continuous frames unavailable");wsi_report_fail(s,DM_WSI_STAGE_TIMER,DM_WSI_CODE_UNKNOWN,0,0);wsi_report_snapshot(s,"resume timer start failed");}
        else wsi_report_progress(s);
    }}
static void focus_changed(ANativeActivity*a,int focused){app_state*s=a->instance;if(!s)return;
    s->focused=focused?1:0;
    if(s->arbiter&&!dm_input_arbiter_focus(s->arbiter,focused?1:0))LOG("arbiter focus invalidation rejected");
    if(!focused)gamepad_rearm(s);
    update_touch_state(s);
    if(!focused)touch_consume(s,"focus_lost");
}
static void configuration_changed(ANativeActivity*a){app_state*s=a->instance;if(!s)return;
    float density=activity_density(a);
    if(density==s->density)return;
    s->density=density;
    if(s->window)touch_resize_window(s,s->window);
    else if(s->arbiter){
        float width=s->touch_width_pt>0?s->touch_width_pt:667;
        float height=s->touch_height_pt>0?s->touch_height_pt:375;
        int64_t now=monotonic_ns();if(now<0)now=0;
        if(!dm_input_arbiter_set_geometry(s->arbiter,width,height,density,now))
            LOG("arbiter density update rejected");
        else { s->touch_width_pt=width; s->touch_height_pt=height; s->touch_density=density; }
        gamepad_rearm(s);
    }
    update_touch_state(s);
}
static int jni_stage_failed(JNIEnv*env,const char*stage){
    if(!(*env)->ExceptionCheck(env))return 0;
    (*env)->ExceptionDescribe(env);
    (*env)->ExceptionClear(env);
    LOG("controller hotplug JNI failure at %s",stage);
    return 1;
}
static void native_controller_event(JNIEnv*env,jclass cls,jlong session,jint event,jint device_id){
    (void)env;(void)cls;
    controller_hotplug_dispatch(g_controller_app,(uint64_t)session,(int)event,(int)device_id);
}
/* Load the packaged helper through the Activity class loader, register the
   static callback, and register the platform listener. Every JNI failure is
   reported and leaves no claimed registration. */
static int controller_hotplug_start(app_state*s){
    if(!s||!s->activity||!s->activity->env||!s->activity->clazz)return 0;
    if(!controller_hotplug_acquire_owner(s)){
        LOG("controller hotplug owner busy; registration rejected");
        return 0;
    }
    JNIEnv*env=s->activity->env;
    jobject activity_class=NULL,loader=NULL,loader_class=NULL;
    jstring class_name=NULL;
    jclass boundary_class=NULL;
    jobject boundary=NULL;
    jmethodID get_loader=NULL,load_class=NULL,constructor=NULL,register_method=NULL,release_method=NULL;
    uint64_t session=++g_controller_next_session;
    if(!session)session=++g_controller_next_session;
    int result=0;
    int registered=0;
    if(!(activity_class=(*env)->GetObjectClass(env,s->activity->clazz)))goto fail;
    if(!(get_loader=(*env)->GetMethodID(env,activity_class,"getClassLoader","()Ljava/lang/ClassLoader;"))||jni_stage_failed(env,"activity getClassLoader signature"))goto fail;
    if(!(loader=(*env)->CallObjectMethod(env,s->activity->clazz,get_loader))||jni_stage_failed(env,"activity class loader"))goto fail;
    if(!(loader_class=(*env)->GetObjectClass(env,loader)))goto fail;
    if(!(load_class=(*env)->GetMethodID(env,loader_class,"loadClass","(Ljava/lang/String;)Ljava/lang/Class;"))||jni_stage_failed(env,"loadClass signature"))goto fail;
    if(!(class_name=(*env)->NewStringUTF(env,"org.d2rmobile.android.diagnostic.ControllerHotplugBoundary")))goto fail;
    if(!(boundary_class=(jclass)(*env)->CallObjectMethod(env,loader,load_class,class_name))||jni_stage_failed(env,"activity class loader load"))goto fail;
    {
        JNINativeMethod method={"nativeControllerEvent","(JII)V",(void*)native_controller_event};
        if((*env)->RegisterNatives(env,boundary_class,&method,1)!=0||jni_stage_failed(env,"RegisterNatives"))goto fail;
    }
    if(!(constructor=(*env)->GetMethodID(env,boundary_class,"<init>","(Landroid/app/Activity;J)V"))||jni_stage_failed(env,"constructor signature"))goto fail;
    if(!(register_method=(*env)->GetMethodID(env,boundary_class,"register","()Z"))||jni_stage_failed(env,"register signature"))goto fail;
    if(!(release_method=(*env)->GetMethodID(env,boundary_class,"release","()Z"))||jni_stage_failed(env,"release signature"))goto fail;
    if(!(boundary=(*env)->NewObject(env,boundary_class,constructor,s->activity->clazz,(jlong)session))||jni_stage_failed(env,"constructor"))goto fail;
    s->controller_session=session;
    g_controller_app=s;
    if(!(*env)->CallBooleanMethod(env,boundary,register_method)||jni_stage_failed(env,"register")){
        LOG("controller hotplug registration failed");
        goto fail;
    }
    registered=1;
    s->controller_boundary=(*env)->NewGlobalRef(env,boundary);
    if(!s->controller_boundary){
        jni_stage_failed(env,"boundary global ref");
        goto fail;
    }
    s->controller_class=(jclass)(*env)->NewGlobalRef(env,boundary_class);
    if(!s->controller_class){
        jni_stage_failed(env,"class global ref");
        goto fail;
    }
    s->controller_release=release_method;
    LOG("controller hotplug registered session=%llu",(unsigned long long)session);
    result=1;
    goto done;
fail:
    if((*env)->ExceptionCheck(env))(*env)->ExceptionClear(env);
    if(boundary&&registered&&release_method){
        (void)(*env)->CallBooleanMethod(env,boundary,release_method);
        if((*env)->ExceptionCheck(env))(*env)->ExceptionClear(env);
    }
    controller_hotplug_release_owner(s);
    s->controller_session=0;
    if(s->controller_boundary){(*env)->DeleteGlobalRef(env,s->controller_boundary);s->controller_boundary=NULL;}
    if(s->controller_class){(*env)->DeleteGlobalRef(env,s->controller_class);s->controller_class=NULL;}
    s->controller_release=NULL;
    LOG("controller hotplug unavailable; lifecycle retirement remains active");
done:
    if(boundary)(*env)->DeleteLocalRef(env,boundary);
    if(boundary_class)(*env)->DeleteLocalRef(env,boundary_class);
    if(class_name)(*env)->DeleteLocalRef(env,class_name);
    if(loader_class)(*env)->DeleteLocalRef(env,loader_class);
    if(loader)(*env)->DeleteLocalRef(env,loader);
    if(activity_class)(*env)->DeleteLocalRef(env,activity_class);
    return result;
}
static void controller_hotplug_stop(app_state*s){
    if(!s)return;
    controller_hotplug_release_owner(s);
    s->controller_session=0;
    JNIEnv*env=(s->activity&&s->activity->env)?s->activity->env:NULL;
    if(s->controller_boundary&&env){
        if(s->controller_release){
            jboolean released=(*env)->CallBooleanMethod(env,s->controller_boundary,s->controller_release);
            if(jni_stage_failed(env,"release"))LOG("controller hotplug release JNI failed");
            else if(!released)LOG("controller hotplug unregister reported failure");
        }
        (*env)->DeleteGlobalRef(env,s->controller_boundary);
        s->controller_boundary=NULL;
    }
    if(s->controller_class&&env){
        (*env)->DeleteGlobalRef(env,s->controller_class);
        s->controller_class=NULL;
    }
    s->controller_release=NULL;
}
__attribute__((visibility("default"))) void ANativeActivity_onCreate(ANativeActivity*a,void*saved,size_t saved_size){
    (void)saved;(void)saved_size;app_state*s=calloc(1,sizeof(*s));if(!s){ANativeActivity_finish(a);return;}a->instance=s;dm_touch_hud_frame_init(&s->hud_frame);
    s->activity=a;s->timer_fd=-1;s->frame_fd=-1;s->wsi_last_logged=-1;s->resumed=0;s->focused=0;
    dm_view_policy_init(&s->view);
    dm_input_policy_init(&s->gate);
    // #143 显式 WSI 选择：环境变量 DM_DIAGNOSTIC_WSI=1 或内部数据目录中的 wsi-mode 标记；默认仍是 CPU 复制路径。
    // #143 explicit WSI selection: env DM_DIAGNOSTIC_WSI=1 or a wsi-mode marker in the internal data dir; the default stays the CPU-copy path.
    {
        char marker[4096];int wsi_mode=0;
        const char*env=getenv("DM_DIAGNOSTIC_WSI");
        if(env&&env[0]=='1'&&env[1]=='\0')wsi_mode=1;
        if(!wsi_mode&&a->internalDataPath&&
           snprintf(marker,sizeof marker,"%s/wsi-mode",a->internalDataPath)<(int)sizeof marker&&
           access(marker,F_OK)==0)wsi_mode=1;
        dm_wsi_ops wsi_ops={.ctx=s,.open=wsi_open,.close=wsi_close,.frame=wsi_frame};
        dm_wsi_init(&s->wsi,&wsi_ops,wsi_mode);
        {int64_t now=monotonic_ns();dm_wsi_report_init(&s->report,wsi_mode,now<0?0:(uint64_t)now);}
        // 打开尝试只由控制器的真实打开边界上报；redraw 快路径不计数。
        // Open attempts are reported by the controller's real open boundary only; the redraw fast path never counts.
        dm_wsi_set_open_observer(&s->wsi,wsi_open_observed,s);
        LOG("renderer=%s",wsi_mode?"wsi-clear-swapchain(explicit)":"cpu-copy-indexed(default)");
    }
    a->callbacks->onDestroy=destroy;a->callbacks->onLowMemory=low_memory;
    a->callbacks->onNativeWindowCreated=window_ready;a->callbacks->onNativeWindowRedrawNeeded=window_ready;
    a->callbacks->onNativeWindowDestroyed=window_destroyed;a->callbacks->onNativeWindowResized=window_resized;
    a->callbacks->onPause=pause_app;a->callbacks->onResume=resume_app;
    a->callbacks->onWindowFocusChanged=focus_changed;a->callbacks->onConfigurationChanged=configuration_changed;
    a->callbacks->onInputQueueCreated=input_created;a->callbacks->onInputQueueDestroyed=input_destroyed;
    mkdir(a->internalDataPath,0700);
    dm_app_host_ops ops={.ctx=s,.load=host_load,.stop=host_stop,.pause=host_pause,.resume=host_resume,
        .surface_acquire=host_surface_acquire,.surface_release=host_surface_release};
    s->lifecycle=dm_app_lifecycle_create(&ops,lifecycle_state,s);
    if(!s->lifecycle){LOG("app lifecycle create failed");ANativeActivity_finish(a);return;}
    {
        dm_wsi_lifecycle_ops bridge_ops={.ctx=s->lifecycle,.surface_created=bridge_surface_created,
            .surface_destroyed=bridge_surface_destroyed,.trace=bridge_trace};
        dm_wsi_bridge_init(&s->wsi_bridge,&s->wsi,&bridge_ops);
    }
    if(!dm_app_lifecycle_start(s->lifecycle)){
        LOG("app lifecycle start failed: state=%s",dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)));
        ANativeActivity_finish(a);return;
    }
    s->density=activity_density(a);
    s->arbiter=dm_input_arbiter_create(667,375,s->density);
    if(!s->arbiter){LOG("input arbiter create failed");ANativeActivity_finish(a);return;}
    s->touch_width_pt=667;s->touch_height_pt=375;s->touch_density=s->density;
    if(!controller_hotplug_start(s))LOG("controller hotplug listener not registered; no hotplug claim");
    LOG("app backend=%s state=%s touch=density %.2f",dm_app_lifecycle_backend_name(),
        dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)),s->density);
}
