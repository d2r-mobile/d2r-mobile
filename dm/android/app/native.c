// NativeActivity 工程诊断：APK 域内合成 ARM64 门往返与自有索引纹理 Vulkan 场景，窗口只在就绪回调期间借用，不运行游戏。
// NativeActivity engineering diagnostics: synthetic ARM64 gates plus an owned indexed-textured Vulkan scene inside the APK domain, borrowing windows only while the lifecycle is ready; never runs the game.
#include "../artifact.h"
#include "../vulkan/vk_host.h"
#include "../vulkan/vk_indexed_textured.h"
#include "diagnostic_scene.h"
#include "input_policy.h"
#include "lifecycle.h"
#include "present_policy.h"
#include "touch_bridge.h"
#include "touch_hud.h"
#include "touch_hud_overlay.h"
#include "view_policy.h"
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
typedef struct{dm_vk vk;AInputQueue*input;dm_app_lifecycle*lifecycle;ANativeActivity*activity;ANativeWindow*window;dm_touch_bridge*touch;dm_view_policy view;dm_input_policy gate;ALooper*looper;int timer_fd;int resumed,focused,touch_allowed;float density,touch_width_pt,touch_height_pt;uint64_t last_touch_buttons;int32_t last_touch_lx,last_touch_ly,last_touch_rx,last_touch_ry;int64_t last_touch_log_ns;dm_android_touch_observation hud_obs;dm_touch_hud_frame hud_frame;int hud_obs_valid;char vertex[4096],fragment[4096];}app_state;
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
static void lifecycle_state(void*ctx,dm_app_state state){
    (void)ctx;
    LOG("app lifecycle state=%s",dm_app_lifecycle_state_name(state));
}
static int64_t monotonic_ns(void){
    struct timespec ts;
    if(clock_gettime(CLOCK_MONOTONIC,&ts))return -1;
    return (int64_t)ts.tv_sec*1000000000LL+ts.tv_nsec;
}
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
    if(!s||!s->touch||s->density<=0){if(s){s->hud_obs_valid=0;dm_touch_hud_frame_suppress(&s->hud_frame);}return;}
    dm_android_touch_observation o;
    if(!dm_touch_bridge_observe(s->touch,s->density,&o)){
        s->hud_obs_valid=0;dm_touch_hud_frame_suppress(&s->hud_frame);return;
    }
    if(!s->hud_obs_valid||memcmp(&s->hud_obs,&o,sizeof o)!=0){
        s->hud_obs=o;s->hud_obs_valid=1;dm_touch_hud_frame_dirty(&s->hud_frame);
    }
}
/* Production CPU overlay seam. The platform-neutral module validates, clips
   and writes only visible pixels; this wrapper only adapts the Android buffer. */
static int draw_hud_overlay(app_state*s,ANativeWindow_Buffer*buffer){
    if(!s||!s->touch||!buffer||!buffer->bits||!buffer->width||!buffer->height)return 0;
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
    if(!s||!s->touch)return;
    touch_refresh_hud(s);
    dm_npad_state state=dm_touch_bridge_read(s->touch);
    if(state.attributes!=1)return;
    int64_t now=monotonic_ns();
    int changed=state.buttons!=s->last_touch_buttons||
        state.lx!=s->last_touch_lx||state.ly!=s->last_touch_ly||
        state.rx!=s->last_touch_rx||state.ry!=s->last_touch_ry;
    if(changed||s->last_touch_log_ns==0||
       (now>=0&&now-s->last_touch_log_ns>=250000000LL)){
        const dm_touch_diag*diag=dm_touch_bridge_diag(s->touch);
        LOG("touch %s buttons=%llu lx=%d ly=%d rx=%d ry=%d diag_reads=%llu invalid=%llu rejected=%llu drained=%llu up_retries=%llu cancel_fallbacks=%llu",
            reason,(unsigned long long)state.buttons,state.lx,state.ly,state.rx,state.ry,
            (unsigned long long)(diag?diag->diagnostic_reads:0),
            (unsigned long long)(diag?diag->invalid_events:0),
            (unsigned long long)(diag?diag->rejected_events:0),
            (unsigned long long)(diag?diag->drained_pulses:0),
            (unsigned long long)(diag?diag->up_retries:0),
            (unsigned long long)(diag?diag->cancel_fallbacks:0));
        s->last_touch_log_ns=now;
        s->last_touch_buttons=state.buttons;
        s->last_touch_lx=state.lx;s->last_touch_ly=state.ly;
        s->last_touch_rx=state.rx;s->last_touch_ry=state.ry;
    }
}
static void touch_cancel(app_state*s,const char*reason,int64_t now){
    if(!s||!s->touch)return;
    if(!dm_touch_bridge_cancel_all(s->touch,now))
        LOG("touch cancel rejected: %s",reason);
    touch_consume(s,reason);
}
static void touch_resize_window(app_state*s,ANativeWindow*window){
    if(!s||!s->touch||!window)return;
    int width=ANativeWindow_getWidth(window),height=ANativeWindow_getHeight(window);
    if(width<=0||height<=0||s->density<=0)return;
    if(!dm_view_policy_set_view(&s->view,(uint32_t)width,(uint32_t)height,s->density))return;
    float width_pt=s->view.view_width_pt,height_pt=s->view.view_height_pt;
    if(width_pt==s->touch_width_pt&&height_pt==s->touch_height_pt)return;
    int64_t now=monotonic_ns();if(now<0)now=0;
    if(!dm_touch_bridge_resize(s->touch,width_pt,height_pt,now)){
        LOG("touch resize rejected: %.1fx%.1f pt",width_pt,height_pt);return;
    }
    s->touch_width_pt=width_pt;s->touch_height_pt=height_pt;
    touch_consume(s,"resize");
}
// 诊断读回目标尺寸；窗口缓冲尺寸与它无关（居中裁剪由 app/present_policy.c 决定）。
// Diagnostic readback extent; the window buffer size is independent and handled by the centred crop in app/present_policy.c.
#define DM_DIAG_TARGET_EXTENT 256u
static void draw_diagnostic(ANativeActivity*a,ANativeWindow*window,int external_redraw){
    app_state*s=a->instance;if(!s||!window)return;
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
    int ticked=dm_touch_bridge_tick(s->touch,now);
    if(ticked)touch_consume(s,"timer");
    if(dm_touch_hud_frame_timer_ready(&s->hud_frame,s->window!=NULL&&s->touch_allowed))
        draw_diagnostic(s->activity,s->window,0);
    return 1;
}
static int start_input_timer(app_state*s){
    if(!s||!s->touch)return 0;
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
static void update_touch_state(app_state*s){
    if(!s)return;
    dm_input_policy_set_resumed(&s->gate,s->resumed);
    dm_input_policy_set_focused(&s->gate,s->focused);
    dm_input_policy_set_queue(&s->gate,s->input!=NULL);
    dm_input_policy_set_window(&s->gate,s->window!=NULL);
    dm_input_policy_set_touch(&s->gate,s->touch!=NULL);
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
    if(!s||!s->touch)return;
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
    if(!dm_touch_bridge_event(s->touch,&translated))
        LOG("touch event rejected action=%d count=%u",action,count);
    touch_consume(s,"event");
}
static int input_ready(int fd,int events,void*data){
    (void)fd;(void)events;ANativeActivity*a=data;app_state*s=a->instance;if(!s||!s->input)return 0;
    AInputEvent*event;while(AInputQueue_getEvent(s->input,&event)>=0){
        if(AInputQueue_preDispatchEvent(s->input,event))continue;int handled=0;
        if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_KEY&&AKeyEvent_getKeyCode(event)==AKEYCODE_BACK){
            if(AKeyEvent_getAction(event)==AKEY_EVENT_ACTION_UP)ANativeActivity_finish(a);handled=1;
        }else if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_MOTION){
            handle_motion(s,event);handled=1;
        }
        AInputQueue_finishEvent(s->input,event,handled);
    }return 1;
}
static void input_created(ANativeActivity*a,AInputQueue*queue){app_state*s=a->instance;if(!s)return;
    if(s->input==queue){update_touch_state(s);return;}
    if(s->input){
        AInputQueue_detachLooper(s->input);s->input=NULL;
        update_touch_state(s);
        if(s->touch){dm_npad_state neutral;dm_touch_bridge_destroy(s->touch,&neutral);s->touch=NULL;
            LOG("touch bridge replaced on queue change buttons=%llu",(unsigned long long)neutral.buttons);}
    }
    if(!s->touch){
        s->density=activity_density(a);
        s->touch=dm_touch_bridge_create(667,375,s->density);
        if(!s->touch){LOG("touch bridge recreate failed");return;}
        LOG("touch game provider unavailable: production scene/hit/action disabled");
        s->touch_width_pt=667;s->touch_height_pt=375;
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
    if(s->touch){dm_npad_state neutral;dm_touch_bridge_destroy(s->touch,&neutral);s->touch=NULL;
        LOG("touch bridge destroyed on input queue loss buttons=%llu",(unsigned long long)neutral.buttons);}
}
static void window_ready(ANativeActivity*a,ANativeWindow*window){
    app_state*s=a->instance;if(!s||!s->lifecycle)return;
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
static void window_destroyed(ANativeActivity*a,ANativeWindow*window){
    app_state*s=a->instance;if(!s||!s->lifecycle)return;
    if(s->window==window){s->window=NULL;update_touch_state(s);
        s->hud_obs_valid=0;dm_touch_hud_frame_suppress(&s->hud_frame);
        int64_t now=monotonic_ns();touch_cancel(s,"window_destroyed",now<0?0:now);}
    if(!dm_app_lifecycle_surface_destroyed(s->lifecycle,window))
        LOG("stale surface destroy ignored");
}
static void destroy(ANativeActivity*a){app_state*s=a->instance;if(s){
    stop_input_timer(s);
    if(s->input){AInputQueue_detachLooper(s->input);s->input=NULL;}
    if(s->touch){dm_npad_state neutral;dm_touch_bridge_destroy(s->touch,&neutral);s->touch=NULL;
        LOG("touch bridge destroyed buttons=%llu",(unsigned long long)neutral.buttons);}
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
    int64_t now=monotonic_ns();touch_cancel(s,"pause",now<0?0:now);
    if(!dm_app_lifecycle_pause(s->lifecycle))LOG("pause rejected: state=%s",dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)));}
static void resume_app(ANativeActivity*a){app_state*s=a->instance;if(!s||!s->lifecycle)return;
    if(!dm_app_lifecycle_resume(s->lifecycle)){LOG("resume rejected: state=%s",dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)));return;}
    s->resumed=1;update_touch_state(s);}
static void focus_changed(ANativeActivity*a,int focused){app_state*s=a->instance;if(!s)return;
    s->focused=focused?1:0;update_touch_state(s);
    if(!focused){int64_t now=monotonic_ns();touch_cancel(s,"focus_lost",now<0?0:now);}
}
static void configuration_changed(ANativeActivity*a){app_state*s=a->instance;if(!s)return;
    float density=activity_density(a);
    if(density==s->density)return;
    s->density=density;
    if(s->touch)dm_touch_bridge_set_density(s->touch,density);
    s->touch_width_pt=0;s->touch_height_pt=0;
    if(s->window)touch_resize_window(s,s->window);
    update_touch_state(s);
}
__attribute__((visibility("default"))) void ANativeActivity_onCreate(ANativeActivity*a,void*saved,size_t saved_size){
    (void)saved;(void)saved_size;app_state*s=calloc(1,sizeof(*s));if(!s){ANativeActivity_finish(a);return;}a->instance=s;dm_touch_hud_frame_init(&s->hud_frame);
    s->activity=a;s->timer_fd=-1;s->resumed=0;s->focused=0;
    dm_view_policy_init(&s->view);
    dm_input_policy_init(&s->gate);
    a->callbacks->onDestroy=destroy;a->callbacks->onLowMemory=low_memory;
    a->callbacks->onNativeWindowCreated=window_ready;a->callbacks->onNativeWindowRedrawNeeded=window_ready;
    a->callbacks->onNativeWindowDestroyed=window_destroyed;a->callbacks->onNativeWindowResized=window_ready;
    a->callbacks->onPause=pause_app;a->callbacks->onResume=resume_app;
    a->callbacks->onWindowFocusChanged=focus_changed;a->callbacks->onConfigurationChanged=configuration_changed;
    a->callbacks->onInputQueueCreated=input_created;a->callbacks->onInputQueueDestroyed=input_destroyed;
    mkdir(a->internalDataPath,0700);
    dm_app_host_ops ops={.ctx=s,.load=host_load,.stop=host_stop,.pause=host_pause,.resume=host_resume,
        .surface_acquire=host_surface_acquire,.surface_release=host_surface_release};
    s->lifecycle=dm_app_lifecycle_create(&ops,lifecycle_state,s);
    if(!s->lifecycle){LOG("app lifecycle create failed");ANativeActivity_finish(a);return;}
    if(!dm_app_lifecycle_start(s->lifecycle)){
        LOG("app lifecycle start failed: state=%s",dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)));
        ANativeActivity_finish(a);return;
    }
    s->density=activity_density(a);
    s->touch=dm_touch_bridge_create(667,375,s->density);
    if(!s->touch){LOG("touch bridge create failed");ANativeActivity_finish(a);return;}
    s->touch_width_pt=667;s->touch_height_pt=375;
    LOG("app backend=%s state=%s touch=density %.2f",dm_app_lifecycle_backend_name(),
        dm_app_lifecycle_state_name(dm_app_lifecycle_state(s->lifecycle)),s->density);
}
