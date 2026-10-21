// NativeActivity 工程诊断：APK 域内合成 ARM64 门往返与 Vulkan 图案，窗口只在回调期间借用，不运行游戏。
// NativeActivity engineering diagnostics: synthetic ARM64 gates / Vulkan pattern inside the APK domain, borrow windows only during callbacks, never run the game.
#include "../artifact.h"
#include "../vulkan/vk_host.h"
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/looper.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define LOG(...) __android_log_print(ANDROID_LOG_INFO,"D2R-Diagnostic",__VA_ARGS__)
typedef struct{dm_vk vk;AInputQueue*input;char vertex[4096],fragment[4096];}app_state;
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
    if((ANativeWindow_getWidth(window)!=256||ANativeWindow_getHeight(window)!=256||ANativeWindow_getFormat(window)!=WINDOW_FORMAT_RGBA_8888)&&
        ANativeWindow_setBuffersGeometry(window,256,256,WINDOW_FORMAT_RGBA_8888)){LOG("window geometry failed");free(pixels);return;}
    ANativeWindow_Buffer buffer;
    if(!ANativeWindow_lock(window,&buffer,NULL)){
        int copied=0;
        if(buffer.bits&&buffer.format==WINDOW_FORMAT_RGBA_8888&&buffer.width>=256&&buffer.height>=256&&buffer.stride>=buffer.width){
            for(unsigned y=0;y<256;y++)memcpy((uint8_t*)buffer.bits+(size_t)y*buffer.stride*4,pixels+y*256*4,256*4);
            copied=1;
        }else LOG("unexpected window buffer");
        if(ANativeWindow_unlockAndPost(window))LOG("window post failed");
        else if(copied)LOG("Vulkan graphics diagnostic: 256x256 pixel-check=PASS, CPU-readback window post=PASS; game presenter not implemented");
    }else LOG("window lock failed");
    free(pixels);
}
static int input_ready(int fd,int events,void*data){
    (void)fd;(void)events;ANativeActivity*a=data;app_state*s=a->instance;if(!s||!s->input)return 0;
    AInputEvent*event;while(AInputQueue_getEvent(s->input,&event)>=0){
        if(AInputQueue_preDispatchEvent(s->input,event))continue;int handled=0;
        if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_KEY&&AKeyEvent_getKeyCode(event)==AKEYCODE_BACK){
            if(AKeyEvent_getAction(event)==AKEY_EVENT_ACTION_UP)ANativeActivity_finish(a);handled=1;}
        AInputQueue_finishEvent(s->input,event,handled);
    }return 1;
}
static void input_created(ANativeActivity*a,AInputQueue*queue){app_state*s=a->instance;if(!s)return;
    ALooper*looper=ALooper_forThread();if(!looper)looper=ALooper_prepare(0);
    if(!looper){LOG("input looper unavailable");return;}s->input=queue;AInputQueue_attachLooper(queue,looper,1,input_ready,a);
}
static void input_destroyed(ANativeActivity*a,AInputQueue*queue){AInputQueue_detachLooper(queue);app_state*s=a->instance;if(s&&s->input==queue)s->input=NULL;}
static void destroy(ANativeActivity*a){app_state*s=a->instance;if(s){if(s->input)AInputQueue_detachLooper(s->input);dm_vk_destroy(&s->vk);free(s);a->instance=NULL;}}
static void low_memory(ANativeActivity*a){app_state*s=a->instance;if(s)dm_vk_destroy(&s->vk);}
static void pause_app(ANativeActivity*a){(void)a;LOG("paused");}
static void resume_app(ANativeActivity*a){(void)a;LOG("resumed");}
__attribute__((visibility("default"))) void ANativeActivity_onCreate(ANativeActivity*a,void*saved,size_t saved_size){
    (void)saved;(void)saved_size;app_state*s=calloc(1,sizeof(*s));if(!s){ANativeActivity_finish(a);return;}a->instance=s;
    a->callbacks->onDestroy=destroy;a->callbacks->onLowMemory=low_memory;
    a->callbacks->onNativeWindowCreated=draw;a->callbacks->onNativeWindowRedrawNeeded=draw;
    a->callbacks->onPause=pause_app;a->callbacks->onResume=resume_app;
    a->callbacks->onInputQueueCreated=input_created;a->callbacks->onInputQueueDestroyed=input_destroyed;
    mkdir(a->internalDataPath,0700);
    LOG("synthetic APK-process ARM64 gate smoke=%s",native_smoke(a)?"FAIL":"PASS");
    if(copy_asset(a,"fullscreen.vert.spv",s->vertex)||copy_asset(a,"pixels.frag.spv",s->fragment))LOG("shader asset copy failed");
}
