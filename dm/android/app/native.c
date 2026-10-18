// NativeActivity 工程诊断：APK 域内合成 ARM64 门往返与 Vulkan 图案，窗口只在回调期间借用，不运行游戏。
// NativeActivity engineering diagnostics: synthetic ARM64 gates / Vulkan pattern inside the APK domain, borrow windows only during callbacks, never run the game.
#include "../artifact.h"
#include "../vulkan/vk_host.h"
#include "input_policy.h"
#include "lifecycle.h"
#include "touch_bridge.h"
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"D2R-Diagnostic",__VA_ARGS__)
typedef struct{dm_vk vk;AInputQueue*input;dm_app_lifecycle*lifecycle;ANativeActivity*activity;ANativeWindow*window;dm_touch_bridge*touch;dm_view_policy view;dm_input_policy gate;ALooper*looper;int timer_fd;int resumed,focused,touch_allowed;float density,touch_width_pt,touch_height_pt;uint64_t last_touch_buttons;int32_t last_touch_lx,last_touch_ly,last_touch_rx,last_touch_ry;int64_t last_touch_log_ns;char vertex[4096],fragment[4096];}app_state;
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
    if(copy_asset(s->activity,"fullscreen.vert.spv",s->vertex)||
       copy_asset(s->activity,"pixels.frag.spv",s->fragment)){
        LOG("shader asset copy failed");return 1;
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
static void touch_consume(app_state*s,const char*reason){
    if(!s||!s->touch)return;
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
static void draw(ANativeActivity*a,ANativeWindow*window){
    app_state*s=a->instance;if(!s)return;
    if(!s->vk.device){VkResult r=dm_vk_create(&s->vk);if(r!=VK_SUCCESS){LOG("Vulkan create failed: %d",r);return;}}
    uint8_t*pixels=malloc(256u*256u*4u);if(!pixels)return;
    dm_vk_pixels target={.width=256,.height=256,.bytes=pixels,.capacity=256u*256u*4u};
    VkResult r=dm_vk_draw_pixels(&s->vk,s->vertex,s->fragment,&target);
    if(r!=VK_SUCCESS){LOG("Vulkan draw failed: %d",r);free(pixels);return;}
    for(unsigned y=0;y<256;y++)for(unsigned x=0;x<256;x++){
        const uint8_t*p=pixels+(y*256+x)*4;
        if(p[0]!=(x&15u)*17||p[1]!=(y&15u)*17||p[2]!=((x^y)&1u)*255||p[3]!=255){
            LOG("Vulkan pixel mismatch at %u,%u",x,y);free(pixels);return;}
    }
    int window_width=ANativeWindow_getWidth(window),window_height=ANativeWindow_getHeight(window);
    if(window_width>0&&window_height>0)
        (void)dm_view_policy_set_view(&s->view,(uint32_t)window_width,(uint32_t)window_height,s->density);
    ANativeWindow_Buffer buffer;
    if(!ANativeWindow_lock(window,&buffer,NULL)){
        int copied=0;
        if(buffer.bits&&buffer.format==WINDOW_FORMAT_RGBA_8888&&buffer.width>0&&
           buffer.height>0&&buffer.stride>=buffer.width){
            (void)dm_view_policy_set_buffer(&s->view,buffer.width,buffer.height);
            for(uint32_t y=0;y<(uint32_t)buffer.height;y++)
                memset((uint8_t*)buffer.bits+(size_t)y*buffer.stride*4,0,(size_t)buffer.width*4);
            uint32_t copy_w=buffer.width<256?buffer.width:256;
            uint32_t copy_h=buffer.height<256?buffer.height:256;
            uint32_t origin_x=(buffer.width-copy_w)/2,origin_y=(buffer.height-copy_h)/2;
            for(uint32_t y=0;y<copy_h;y++)
                memcpy((uint8_t*)buffer.bits+((size_t)(origin_y+y)*buffer.stride+origin_x)*4,
                       pixels+(size_t)y*256*4,(size_t)copy_w*4);
            copied=1;
        }else LOG("unexpected window buffer");
        if(ANativeWindow_unlockAndPost(window))LOG("window post failed");
        else if(copied)LOG("Vulkan graphics diagnostic: 256x256 pixel-check=PASS, CPU-readback window post=PASS (%dx%d view, %dx%d buffer); game presenter not implemented",
            window_width,window_height,buffer.width,buffer.height);
    }else LOG("window lock failed");
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
    if(dm_touch_bridge_tick(s->touch,now))touch_consume(s,"timer");
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
    if(dm_app_lifecycle_state(s->lifecycle)==DM_APP_STATE_OWNED_DIAGNOSTIC)draw(a,window);
}
static void window_destroyed(ANativeActivity*a,ANativeWindow*window){
    app_state*s=a->instance;if(!s||!s->lifecycle)return;
    if(s->window==window){s->window=NULL;update_touch_state(s);
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
    (void)saved;(void)saved_size;app_state*s=calloc(1,sizeof(*s));if(!s){ANativeActivity_finish(a);return;}a->instance=s;
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
