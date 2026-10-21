// 原游戏宿主的 NativeActivity 前端：真正运行 nnMain 并呈现游戏交换链。
// NativeActivity frontend for the real guest host: execute nnMain and present its swapchain.
#define _GNU_SOURCE
#include "../../host/dm.h"
#include "../../host/hle.h"
#include "../../host/front.h"
#include "../../host/dm_input.h"
#include "../../host/dm_i18n.h"
#include "../../host/dm_hud.h"
#include "debug_stats.h"
#include "performance_hint.h"
#include "launch_settings.h"
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android/input.h>
#include <android/looper.h>
#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdatomic.h>
#include <limits.h>
#include <float.h>
#include <math.h>

typedef struct {
    ANativeActivity *activity; ANativeWindow *window; AInputQueue *input;
    pthread_mutex_t lock; pthread_cond_t ready; pthread_t worker;
    int stop; _Atomic int started; _Atomic int activity_resumed;
    uint64_t pad_buttons,hat_buttons,trigger_buttons,move_keys;int tap_a;int32_t lx,ly,rx,ry;
    uint64_t touch_buttons;int32_t touch_lx,touch_ly,touch_rx,touch_ry;
    pthread_t debug_thread;int debug_started;_Atomic int debug_stop;
    float requested_frame_rate;int frame_rate_attempted;
} game_app;
static game_app *application;
static _Atomic int display_hz;
// 运行时可切的调试浮层（游戏内菜单的「帧率显示」）：UI 线程写，调试线程每拍读。启动初值见 debug_overlay 的读取处。
// Runtime-togglable debug overlay (the in-game menu's "frame rate display"): the UI thread writes it, the debug
// thread reads it every tick. The startup value is read where DM_DEBUG_OVERLAY is.
static _Atomic int debug_overlay_enabled;
// 游戏内菜单是否开着（见 input_ready）：开着就吞掉手柄与按键事件，免得角色在菜单后面继续动。
// Is the in-game menu open (see input_ready): while it is, pad and key events are swallowed so the character does
// not keep moving behind the menu.
static _Atomic int menu_open;
// 游戏内导入存档后要退出：这一次退出**不能**再请游戏存盘 —— 否则会把刚导入的存档用旧的内存状态覆盖掉。
// 一次性的：被 settle_save 消费后清零。
// After importing a save from the in-game menu the game exits, and that exit must NOT ask the game to save -- the
// save just imported would otherwise be overwritten with the stale in-memory state. One-shot: cleared by settle_save.
static _Atomic int skip_save_once;
int dm_android_display_hz(void) {return atomic_load(&display_hz);}
static pthread_once_t frame_rate_once=PTHREAD_ONCE_INIT;
static int32_t (*window_set_frame_rate)(ANativeWindow *,float,int8_t);
static void frame_rate_api_load(void) {
    // API30符号动态解析；API29仍可加载。库句柄保留至进程退出。
    // Resolve the API30 symbol dynamically for API29 loading; retain its library for process lifetime.
    void *library=dlopen("libandroid.so",RTLD_NOW|RTLD_LOCAL);
    if(library){void *symbol=dlsym(library,"ANativeWindow_setFrameRate");memcpy(&window_set_frame_rate,&symbol,sizeof window_set_frame_rate);}
}
typedef struct { ANativeActivity *activity;float rate; } frame_rate_request;
static void *frame_rate_java_request(void *argument) {
    frame_rate_request *request=argument;ANativeActivity *a=request->activity;JNIEnv *env=NULL;int attached=0;
    // 使用真实宿主pthread栈附着ART，不能从已换栈的客体线程直接附着。
    // Attach ART from a real host pthread stack, never from a guest thread's replaced stack.
    if((*a->vm)->GetEnv(a->vm,(void**)&env,JNI_VERSION_1_6)!=JNI_OK){
        if((*a->vm)->AttachCurrentThread(a->vm,&env,NULL)!=JNI_OK){LOG("[android-display-preference] ART attach failed");return NULL;}attached=1;
    }
    jclass klass=(*env)->GetObjectClass(env,a->clazz);
    jmethodID method=klass?(*env)->GetMethodID(env,klass,"requestDisplayFrameRate","(F)V"):NULL;
    if(method&&!(*env)->ExceptionCheck(env))(*env)->CallVoidMethod(env,a->clazz,method,request->rate);
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionDescribe(env);(*env)->ExceptionClear(env);LOG("[android-display-preference] JNI request failed rate=%.6f",request->rate);}
    else if(!method)LOG("[android-display-preference] Java method unavailable");
    else LOG("[android-display-preference] UI request queued rate=%.6f Hz (not measured refresh)",request->rate);
    if(klass)(*env)->DeleteLocalRef(env,klass);if(attached)(*a->vm)->DetachCurrentThread(a->vm);return NULL;
}
void dm_android_window_frame_period(double period) {
    game_app *s=application;if(!s||!isfinite(period)||period<0)return;
    double rate=period>0?1.0/period:0;if(!isfinite(rate)||rate>FLT_MAX)return;
    float requested=(float)rate;if(period>0&&requested<=0)return;
    pthread_once(&frame_rate_once,frame_rate_api_load);
    // 窗口引用由前端持有；锁阻止销毁/替换。每个窗口只在目标变化时调用一次。
    // The frontend owns the window reference; the lock excludes replacement/destruction. Call only on target changes.
    int changed=0;pthread_mutex_lock(&s->lock);
    if(s->window&&(!s->frame_rate_attempted||s->requested_frame_rate!=requested)){
        changed=1;s->frame_rate_attempted=1;s->requested_frame_rate=requested;
        if(window_set_frame_rate){
            int32_t result=window_set_frame_rate(s->window,requested,ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_DEFAULT);
            LOG("[android-frame-rate] requested=%.6f Hz period=%.9f s compatibility=default seamless_only=1 result=%d (request, not measured refresh)",requested,period,result);
        }else LOG("[android-frame-rate] API unavailable requested=%.6f Hz; retain system display policy",requested);
    }
    pthread_mutex_unlock(&s->lock);
    // 不持窗口锁调用JNI；Java只向UI排队，不等待UI，避免暂停回调与游戏线程互等。
    // Call JNI outside the window lock; Java queues without waiting for UI to avoid lifecycle deadlocks.
    if(changed){frame_rate_request request={s->activity,requested};pthread_t thread;
        int error=pthread_create(&thread,NULL,frame_rate_java_request,&request);
        if(error)LOG("[android-display-preference] host thread creation failed error=%d",error);
        else pthread_join(thread,NULL);
    }
}
static struct { pthread_mutex_t lock; pthread_cond_t ready; uint64_t serial; int done,result; char text[4096]; } text_request = {
    .lock=PTHREAD_MUTEX_INITIALIZER,.ready=PTHREAD_COND_INITIALIZER
};
size_t swkbd_u16_to_u8(const uint16_t *,size_t,char *,size_t);
size_t swkbd_u8_to_u16(const char *,size_t,uint16_t *,size_t);
static jstring java_text(JNIEnv *env,const char *text) {
    if(!text)text="";size_t cap=strlen(text)+1;uint16_t *units=malloc(cap*sizeof *units);if(!units)die("Android text allocation");
    size_t length=swkbd_u8_to_u16(text,cap-1,units,cap);jstring result=(*env)->NewString(env,units,length);free(units);return result;
}
__attribute__((visibility("default"))) void Java_org_d2r_android_port_D2RActivity_onTextResult(JNIEnv *env,jclass klass,jlong serial,jstring text,jboolean accepted) {
    (void)klass;pthread_mutex_lock(&text_request.lock);
    if(serial==(jlong)text_request.serial&&!text_request.done){
        text_request.text[0]=0;
        if(accepted&&text){const jchar *units=(*env)->GetStringChars(env,text,NULL);if(units){swkbd_u16_to_u8(units,(*env)->GetStringLength(env,text),text_request.text,sizeof text_request.text);(*env)->ReleaseStringChars(env,text,units);}else accepted=JNI_FALSE;}
        text_request.result=accepted?1:0;text_request.done=1;pthread_cond_signal(&text_request.ready);
    }pthread_mutex_unlock(&text_request.lock);
}
static void *progress_watch(void *argument) {
    game_app *s=argument;ANativeActivity *a=s->activity;JNIEnv *env=NULL;if((*a->vm)->AttachCurrentThread(a->vm,&env,NULL)!=JNI_OK)return NULL;
    jclass klass=(*env)->GetObjectClass(env,a->clazz);jmethodID method=(*env)->GetMethodID(env,klass,"updateDebug","(Ljava/lang/String;)V");
    if(!method){(*env)->ExceptionDescribe(env);(*env)->ExceptionClear(env);(*a->vm)->DetachCurrentThread(a->vm);return NULL;}
    // 电池查询方法一次查找、每轮复用；任一查找失败就整组降级为显示 --，不让缺方法拖垮调试线程。
    // Battery query methods are resolved once and reused; if any lookup fails the whole group degrades to "--" rather than failing the debug thread.
    jmethodID battery_current=NULL,battery_voltage=NULL,battery_level=NULL,battery_status=NULL;
    battery_current=(*env)->GetMethodID(env,klass,"batteryCurrentMa","()I");
    battery_voltage=(*env)->GetMethodID(env,klass,"batteryVoltageMv","()I");
    battery_level=(*env)->GetMethodID(env,klass,"batteryLevelPercent","()I");
    battery_status=(*env)->GetMethodID(env,klass,"batteryStatusValue","()I");
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionDescribe(env);(*env)->ExceptionClear(env);battery_current=battery_voltage=battery_level=battery_status=NULL;}
    // 浮层开关：启动值来自启动界面写进 launch-settings.conf 的 DM_DEBUG_OVERLAY；游戏内菜单可以随时开关
    // （UI 线程写这个原子量，这里每拍读一次并按变化通知 Java 侧显隐）。
    // **缺省关闭**（用户要求：调试浮层全局默认不显示），只有配置明确写 1 才打开；写 0 或没写都是关。
    // The overlay switch starts from DM_DEBUG_OVERLAY, written into launch-settings.conf by the launcher; the
    // in-game menu can toggle it at any time (the UI thread writes this atomic, this loop reads it every tick and
    // tells the Java side to show/hide when it changes). **Off by default** (the user asked for the debug overlay to
    // be off globally): only an explicit 1 turns it on; 0 or an absent key both mean off.
    const char *overlay_setting=getenv("DM_DEBUG_OVERLAY");
    atomic_store(&debug_overlay_enabled,overlay_setting&&!strcmp(overlay_setting,"1"));
    jmethodID overlay_method=(*env)->GetMethodID(env,klass,"setDebugOverlay","(Z)V");
    if((*env)->ExceptionCheck(env)){(*env)->ExceptionDescribe(env);(*env)->ExceptionClear(env);overlay_method=NULL;}
    int overlay=atomic_load(&debug_overlay_enabled);
    if(overlay_method)(*env)->CallVoidMethod(env,a->clazz,overlay_method,(jboolean)(overlay!=0));
    else LOG("[android] debug overlay switch unavailable; showing the overlay");
    int overlay_applied=overlay;
    uint64_t previous=0;unsigned unchanged=0;
    for(unsigned i=0;!atomic_load(&s->debug_stop);i++){usleep(500000);dm_android_debug_stats stats;dm_android_debug_get(&stats);dm_hud_info hud;dm_hud_get(&hud);
        // 菜单改了浮层开关就跟着变：显示/隐藏交给 Java 侧，这里只决定还要不要产出文字。
        // Follow the menu's overlay switch: the Java side owns show/hide, this loop only decides whether to produce text.
        if((overlay=atomic_load(&debug_overlay_enabled))!=overlay_applied){overlay_applied=overlay;if(overlay_method)(*env)->CallVoidMethod(env,a->clazz,overlay_method,(jboolean)(overlay!=0));}
        pthread_mutex_lock(&text_request.lock);int keyboard=text_request.serial&&!text_request.done;pthread_mutex_unlock(&text_request.lock);
        // 浮层关掉时整块显示工作都跳过：不查电池、不读 /proc、不拼字符串、不碰 UI 线程。
        // 但下面的首帧看门狗必须照跑，它跟显示无关。
        // With the overlay off this whole display block is skipped: no battery query, no /proc read, no string
        // building, no UI-thread call. The first-frame watchdog below must still run; it is not a display concern.
        if(overlay){
            int dm_loadscreen_up(void);const char *stage=dm_ui(keyboard?"dmAndroidStageText":dm_loadscreen_up()?"dmAndroidStageLoading":hud.valid?"dmAndroidStageGame":stats.frame?"dmAndroidStageMenu":"dmAndroidStageBoot");
            unsigned long pages=0;FILE *memory=fopen("/proc/self/statm","r");if(memory){unsigned long total;fscanf(memory,"%lu %lu",&total,&pages);fclose(memory);}
            char gpu[32];if(stats.gpu_valid)snprintf(gpu,sizeof gpu,"%.1f",stats.gpu_ms);else snprintf(gpu,sizeof gpu,"--");
            // 电池读数经框架获取（应用不需要 root）；读不到就明确显示 --，不伪造数值。
            // Battery readings come from the framework (no root needed); anything unavailable shows "--" rather than a made-up number.
            char power[160];
            if(battery_current&&battery_voltage&&battery_level&&battery_status){
                int milliamps=(*env)->CallIntMethod(env,a->clazz,battery_current);
                int millivolts=(*env)->CallIntMethod(env,a->clazz,battery_voltage);
                int capacity=(*env)->CallIntMethod(env,a->clazz,battery_level);
                int status=(*env)->CallIntMethod(env,a->clazz,battery_status);
                if((*env)->ExceptionCheck(env)){(*env)->ExceptionDescribe(env);(*env)->ExceptionClear(env);milliamps=-1;}
                char value[64];
                if(milliamps<0)snprintf(value,sizeof value,"--");
                else if(millivolts>0){double volts=millivolts/1000.0;snprintf(value,sizeof value,"%d mA | %.2f V | %.1f W",milliamps,volts,milliamps*volts/1000.0);}
                else snprintf(value,sizeof value,"%d mA",milliamps);
                // 本机放电时 HAL 也返回正值，方向不能靠符号判断；充电与否用框架状态（2 充电 / 5 已满）决定。
                // This HAL reports a positive value while discharging too, so direction comes from the framework status (2 charging / 5 full), not the sign.
                int charging=status==2||status==5;
                snprintf(power,sizeof power,dm_ui(charging?"dmAndroidCharging":"dmAndroidPower"),value,capacity<0?0.0:(double)capacity);
            }else snprintf(power,sizeof power,dm_ui("dmAndroidPower"),"--",0.0);
            char text[1024];snprintf(text,sizeof text,dm_ui("dmAndroidDebugMetrics"),stats.frame_ms>0?1000/stats.frame_ms:0,stats.frame_ms,stats.cpu_ms,gpu,stats.wait_ms,power,stats.draws,stats.passes,stats.submits,stats.snapshot_mb,stats.buffer_mb,pages*sysconf(_SC_PAGESIZE)/1048576.,stage,hud.area,hud.x,hud.y,(unsigned long long)dm_input_samples());
            jstring value=java_text(env,text);(*env)->CallVoidMethod(env,a->clazz,method,value);(*env)->DeleteLocalRef(env,value);
            if((*env)->ExceptionCheck(env)){(*env)->ExceptionDescribe(env);(*env)->ExceptionClear(env);break;}
        }
        // 生命周期读数每 10 秒一行：3D 比例决定"抬分辨率到底有没有让 3D 变清"（掉帧时控制器会把内部比例压回去），
        // 客体时钟决定"后台那一整段有没有被算进游戏时间"（不该算）。两个都是判断切后台/回前台是否正常的直接读数，
        // 而上面那条进度行只在前 48 秒打（它兼首帧看门狗），覆盖不到进局与切后台之后。
        // A lifecycle reading every 10 seconds: the 3D scale decides whether raising the resolution really sharpened
        // the 3D world (the controller scales the internal ratio back down on dropped frames), the guest clock
        // whether the background period was counted into game time (it must not be). Both are the direct readings
        // for judging a background/return cycle, and the progress line above only covers the first 48 seconds (it
        // doubles as the first-frame watchdog), so it never reaches the run or anything after a background switch.
        if(i%20==0&&i>=96){uint64_t frame=dm_nvn_frame_count();LOG("[android] 运行状态 frame=%llu 输入样本=%llu 3D=%.2f 客体时钟=%.1f s",(unsigned long long)frame,(unsigned long long)dm_input_samples(),dm_3d_scale_cur(),guest_ns()/1e9);}
        if(i<96&&i%4==0){uint64_t frame=dm_nvn_frame_count();LOG("[android] game progress frame=%llu input_samples=%llu",(unsigned long long)frame,(unsigned long long)dm_input_samples());
            if(frame&&frame==previous&&!keyboard){if(++unchanged==4)crash_dump_now("Android first-frame progress stalled");}else unchanged=0;previous=frame;}
    }
    (*env)->DeleteLocalRef(env,klass);(*a->vm)->DetachCurrentThread(a->vm);return NULL;
}

static void copy_asset(ANativeActivity *a,const char *directory,const char *name) {
    AAsset *asset=AAssetManager_open(a->assetManager,name,AASSET_MODE_STREAMING);if(!asset)die("Missing game metadata asset %s",name);
    char path[1024];snprintf(path,sizeof path,"%s/%s",directory,name);FILE *file=fopen(path,"wb");if(!file)die("Game metadata open %s",path);
    char buffer[4096];int n;while((n=AAsset_read(asset,buffer,sizeof buffer))>0)if(fwrite(buffer,1,n,file)!=(size_t)n)die("Game metadata write");fclose(file);AAsset_close(asset);
}
static void *boot(void *argument) {
    game_app *s=argument;pthread_mutex_lock(&s->lock);while(!s->window&&!s->stop)pthread_cond_wait(&s->ready,&s->lock);int stop=s->stop;pthread_mutex_unlock(&s->lock);if(stop)return NULL;
    char path[1024],metadata[1024],log[1024];snprintf(metadata,sizeof metadata,"%s/game",s->activity->internalDataPath);mkdir(metadata,0700);
    snprintf(log,sizeof log,"%s/game.log",s->activity->internalDataPath);int fd=open(log,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);if(fd>=0){dup2(fd,STDERR_FILENO);dup2(fd,STDOUT_FILENO);close(fd);}
    // ART 查询只在游戏开跑前的真实宿主 pthread 上进行；失败保留0，不伪造屏幕信息。
    // Query ART on the real host pthread before starting the guest; retain zero on failure rather than invent display information.
    {
        ANativeActivity *a=s->activity;JNIEnv *env=NULL;int attached=0,hz=0;float rate=0;
        if((*a->vm)->GetEnv(a->vm,(void**)&env,JNI_VERSION_1_6)!=JNI_OK){if((*a->vm)->AttachCurrentThread(a->vm,&env,NULL)==JNI_OK)attached=1;else env=NULL;}
        if(env){jclass klass=(*env)->GetObjectClass(env,a->clazz);jmethodID method=klass?(*env)->GetMethodID(env,klass,"getMaximumDisplayRefreshRate","()F"):NULL;
            if(method&&!(*env)->ExceptionCheck(env))rate=(*env)->CallFloatMethod(env,a->clazz,method);
            if((*env)->ExceptionCheck(env)){(*env)->ExceptionDescribe(env);(*env)->ExceptionClear(env);rate=0;}
            double rounded=(double)rate+.5;if(rate>0&&rounded<=INT_MAX)hz=(int)rounded;
            if(klass)(*env)->DeleteLocalRef(env,klass);if(attached)(*a->vm)->DetachCurrentThread(a->vm);
        }
        atomic_store(&display_hz,hz);LOG("[android] display maximum refresh=%.3f Hz reported=%d Hz (read-only capability query)",rate,hz);
    }
    Dl_info info;if(!dladdr((void*)boot,&info)||!info.dli_fname)die("Native library directory lookup");char library[1024];snprintf(library,sizeof library,"%s",info.dli_fname);char *slash=strrchr(library,'/');if(!slash)die("Native library path");*slash=0;setenv("DM_ANDROID_LIBDIR",library,1);
    // 导入的数据包优先：启动界面把包解到固定的 d2r-package/ 下，只要 manifest.json 在，就用包里的模块、资源和着色器。
    // 否则回落到默认位置：模块来自 APK 自带 asset，romfs 与 shaders 是手工推送到外部目录的那两份。
    // An imported package wins: the launcher unpacks it into a fixed d2r-package/ directory and, whenever
    // manifest.json is present, we use the modules, assets and shaders inside it. Otherwise we fall back to the
    // defaults: modules from the APK's own assets, romfs and shaders as pushed to the external directory by hand.
    char package_manifest[1200];
    int package_length=snprintf(package_manifest,sizeof package_manifest,"%s/d2r-package/manifest.json",s->activity->externalDataPath);
    int imported=package_length>0&&(size_t)package_length<sizeof package_manifest&&access(package_manifest,F_OK)==0;
    if(imported){
        // 模块必须来自内部存储：外部存储是 noexec 挂载（实测 /storage/emulated 带 noexec），从那里 dlopen 会失败。
        // 大资源仍在外部目录，那里放得下 GB 级数据。
        // Modules must come from internal storage: external storage is mounted noexec (measured on
        // /storage/emulated) and dlopen from there fails. The bulk assets stay external, where the gigabytes fit.
        snprintf(metadata,sizeof metadata,"%s/d2r-package/game",s->activity->internalDataPath);mkdir(metadata,0700);
        snprintf(path,sizeof path,"%s/d2r-package/romfs",s->activity->externalDataPath);setenv("DM_ROMFS",path,1);
        snprintf(path,sizeof path,"%s/d2r-package/shaders",s->activity->externalDataPath);setenv("DM_VK_SHADERS",path,1);
        // 模块目录单独一个变量：DM_ANDROID_LIBDIR 归图形驱动找 hook 库用，见 loader.c 的说明。
        // A separate variable for the module directory: DM_ANDROID_LIBDIR belongs to the graphics driver's hook
        // lookup, as documented in loader.c.
        setenv("DM_GUEST_LIBDIR",metadata,1);
        // 三张游戏派生表（编年史仓库 / 经验 / 关联亮度）的内容**不在 APK 里**（壳模式）：由数据包里的 derived.tsv 提供。
        // 没有它表就空着（会打红字），所以这里把"缺文件"说清楚，而不是让人以为是数据本身的问题。
        // The three game-derived tables (chronicle vault / XP / level brightness) are **not compiled into the APK**
        // (shell mode): the package's derived.tsv provides them. Without it the tables stay empty (a red line is
        // printed), so say the file is missing instead of leaving it looking like a data problem.
        snprintf(path,sizeof path,"%s/d2r-package/derived.tsv",s->activity->externalDataPath);
        if(access(path,R_OK)==0){setenv("DM_DERIVED",path,1);LOG("[android] 数据包带 derived.tsv：三张派生表在运行时从它读");}
        else{unsetenv("DM_DERIVED");LOG("[android] 【红】数据包没有 derived.tsv：编年史仓库 / 经验条 / 动态亮度的表将是空的（用新版打包器重新生成数据包）");}
        LOG("[android] imported package in use game=%s",metadata);
    }else{
        const char *modules[]={"main","sdk","subsdk0"};for(unsigned i=0;i<3;i++)for(unsigned j=0;j<2;j++){char name[80];snprintf(name,sizeof name,"%s.%s.tsv",modules[i],j?"gates":"fixups");copy_asset(s->activity,metadata,name);}
        snprintf(path,sizeof path,"%s/romfs",s->activity->externalDataPath);setenv("DM_ROMFS",path,1);
        snprintf(path,sizeof path,"%s/shaders",s->activity->externalDataPath);setenv("DM_VK_SHADERS",path,1);
        unsetenv("DM_GUEST_LIBDIR");
        unsetenv("DM_DERIVED");
        LOG("[android] no imported package; using APK assets and pushed romfs/shaders (derived tables will be empty)");
    }
    snprintf(path,sizeof path,"%s/save",s->activity->internalDataPath);mkdir(path,0700);setenv("DM_SAVE",path,1);
    // 私有可重建的驱动缓存独立于游戏存档；不可写时正常使用内存缓存。
    // Keep rebuildable driver data separate from saves; an unwritable directory falls back to the in-memory cache.
    int cache_path=snprintf(path,sizeof path,"%s/pipeline-cache",s->activity->internalDataPath);
    if(cache_path>0&&(size_t)cache_path<sizeof path&&(!mkdir(path,0700)||errno==EEXIST))setenv("DM_VK_PIPELINE_CACHE_DIR",path,1);
    else{unsetenv("DM_VK_PIPELINE_CACHE_DIR");LOG("[vulkan-pipeline-cache] directory_unavailable=1");}
    snprintf(path,sizeof path,"%s/temp",s->activity->internalDataPath);mkdir(path,0700);setenv("DM_TEMP",path,1);
    // 异常帧日志与 game.log 同目录，便于一起取回；只作诊断，不参与任何游戏状态。
    // The abnormal-frame log sits beside game.log so both are retrieved together; it is diagnostics only and
    // never part of game state.
    setenv("DM_DATA",s->activity->internalDataPath,1);
    // 在游戏首次缓存运行档及输入日志之前应用完整验证过的本应用设置。
    // Apply fully validated application settings before the guest first caches its operation mode and input log.
    dm_android_launch_settings launch;char launch_error[512];
    if(!dm_android_launch_settings_apply(path,&launch,launch_error,sizeof launch_error))die("[android] launch settings: %s",launch_error);
    LOG("[android] launch configuration present=%d operation_mode=%d input_receipts=%d 分辨率=%dx%d",launch.present,launch.operation_mode,launch.input_receipts,launch.resolution_width,launch.resolution_height);
    // 设置生效后立刻记一次环境，确认 DM_GAME_RES 真的传下去了（否则只会在游戏那边看到"未设"）。
    // Immediately record the environment after the settings apply, to confirm DM_GAME_RES really got through;
    // otherwise the only symptom is "unset" on the game side.
    LOG("[android] 环境核对 DM_OPMODE=%s DM_GAME_RES=%s DM_DEBUG_OVERLAY=%s",getenv("DM_OPMODE")?getenv("DM_OPMODE"):"(未设)",getenv("DM_GAME_RES")?getenv("DM_GAME_RES"):"(未设)",getenv("DM_DEBUG_OVERLAY")?getenv("DM_DEBUG_OVERLAY"):"(未设)");
    setenv("DM_TACT_PREFETCH","0",1);setenv("DM_BOOTPROF","1",1);setenv("DM_DBG","1",1);
    setenv("DM_UIMSG","1",1);setenv("DM_MOVIE","1",1);
    __android_log_print(ANDROID_LOG_INFO,"D2RAndroid","Starting real game host; log: %s",log);atomic_store(&s->started,1);
    // 三张派生表（编年史仓库 / 经验 / 亮度）在运行时从 derived.tsv 填：路径是上面才定下来的，晚于库加载时的
    // 构造器，所以这里显式填一次。缺文件时表为空并打红字（不伪造数据）。
    // The three derived tables (vault / XP / brightness) are filled from derived.tsv at run time: the path is only
    // known above, later than the load-time constructors, so fill explicitly here. When the file is missing the
    // tables stay empty and a red line says so (nothing is invented).
    { void dm_hud_exp_load(void); void dm_vault_load(void); void dm_bright_load(void);
      dm_hud_exp_load(); dm_vault_load(); dm_bright_load(); }
    char *args[]={"dmhost-android",metadata,"main","sdk","subsdk0",NULL};int dm_main(int,char**);dm_main(5,args);ANativeActivity_finish(s->activity);return NULL;
}
void front_run(void (*guest)(void)) {void mtl_init(void);mtl_init();game_app *s=application;if(!pthread_create(&s->debug_thread,NULL,progress_watch,s))s->debug_started=1;guest();}
void *front_metal_layer(void) {if(!application)return NULL;pthread_mutex_lock(&application->lock);ANativeWindow *w=application->window;pthread_mutex_unlock(&application->lock);return w;}
// 出图分辨率。与 Mac（front_mac.m）和 iOS（front_ios.m）同一语义：DM_GAME_RES=宽x高 覆盖主机档，
// 掌机档按 720/900 = 0.8 同比缩放；没有该变量时保持原版 16:9 常数（1600x900 / 1280x720），行为不变。
// 注意必须在启动时抬高：窗口贴图按启动时的参数位一次建好，之后游戏只接受不超过它的新值（dm_uiscreen.c），
// 也就是运行时只能缩不能放。另外抬分辨率不等于 3D 渲染跟着变清 —— DM_3D_SCALE_DYN 掉帧时会把 3D 内部比例压回去。
// Output resolution, with the same semantics as Mac (front_mac.m) and iOS (front_ios.m): DM_GAME_RES=<w>x<h>
// overrides the docked profile and the handheld profile scales it by 720/900 = 0.8. Without the variable the
// original 16:9 constants stay (1600x900 / 1280x720), so nothing changes. It must be raised at startup: the window
// texture is built once from the startup parameters and the game only accepts new values up to that size
// (dm_uiscreen.c), so at runtime it can only shrink. Raising it also does not automatically sharpen the 3D world —
// DM_3D_SCALE_DYN scales the internal 3D ratio back down when frames drop.
uint64_t front_game_res(int docked) {
    unsigned w=1600,h=900;const char *e=getenv("DM_GAME_RES");
    if(e&&*e&&sscanf(e,"%ux%u",&w,&h)!=2){LOG("[android] DM_GAME_RES 解析失败，用默认 1600x900：%s",e);w=1600;h=900;}
    static int reported;
    // 前三次都记：分辨率必须在启动时定下来（游戏按启动时的参数位建窗口贴图），而如果第一次被问发生在启动
    // 设置生效之前，只看第一行会看不出这件事 —— 出问题时需要能直接看到"当时读到的是什么"。
    // Log the first three calls: the resolution must be decided at startup (the game builds its window texture from
    // the startup parameter slot), and if the first ask happens before the launch settings take effect, only the
    // first line would hide that — when it goes wrong this shows what was actually read at that moment.
    if(reported<3){reported++;LOG("[android] 出图档位=%s DM_GAME_RES=%s → 主机档 %ux%u / 掌机档 %ux%u (第%d次)",docked?"主机":"掌机",e&&*e?e:"(未设)",w,h,(w*4/5+3)/4*4,h*4/5,reported);}
    return docked?((uint64_t)h<<32)|w:((uint64_t)(h*4/5)<<32)|(((w*4/5)+3)/4*4);
}
static int text_input_request(const char *title,const char *sub,const char *guide,const char *initial,int max,char *out,int cap) {
    if(!application||!out||cap<=0)return -1;ANativeActivity *a=application->activity;JNIEnv *env=NULL;int attached=0;
    if((*a->vm)->GetEnv(a->vm,(void**)&env,JNI_VERSION_1_6)!=JNI_OK){if((*a->vm)->AttachCurrentThread(a->vm,&env,NULL)!=JNI_OK)return -1;attached=1;}
    extern int g_swkbd_name_panel,g_swkbd_numeric_max;int name=g_swkbd_name_panel,numeric=g_swkbd_numeric_max;
    char message[2048];snprintf(message,sizeof message,"%s%s%s%s%s",sub?sub:"",sub&&*sub&&guide&&*guide?"\n":"",guide?guide:"",name?"\n":"",name?dm_ui("dmNameRule"):"");
    if(numeric>=0)snprintf(message,sizeof message,"0 – %d",numeric);
    const char *heading=title&&*title?title:dm_ui(name?"dmNameTitle":numeric>=0?"dmQtyTitle":"dmInputTitle");
    pthread_mutex_lock(&text_request.lock);uint64_t serial=++text_request.serial;text_request.done=0;text_request.result=-1;pthread_mutex_unlock(&text_request.lock);
    // Java 的 UI 调度不持结果锁；回调可以在进入等待前完成。
    // Do not hold the result lock while scheduling Java UI; the callback may complete before the wait starts.
    jclass klass=(*env)->GetObjectClass(env,a->clazz);jmethodID method=(*env)->GetMethodID(env,klass,"requestText","(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;IIZ)V");
    jstring strings[4]={java_text(env,heading),java_text(env,message),java_text(env,name?dm_ui("dmNameBlankRandom"):guide),java_text(env,initial)};
    if(method&&!(*env)->ExceptionCheck(env))(*env)->CallVoidMethod(env,a->clazz,method,(jlong)serial,strings[0],strings[1],strings[2],strings[3],max,numeric,name?JNI_TRUE:JNI_FALSE);
    int failed=!method||(*env)->ExceptionCheck(env);if(failed){(*env)->ExceptionDescribe(env);(*env)->ExceptionClear(env);}
    for(unsigned i=0;i<4;i++)if(strings[i])(*env)->DeleteLocalRef(env,strings[i]);(*env)->DeleteLocalRef(env,klass);if(attached)(*a->vm)->DetachCurrentThread(a->vm);
    if(failed)return -1;
    pthread_mutex_lock(&text_request.lock);while(!text_request.done)pthread_cond_wait(&text_request.ready,&text_request.lock);
    int result=text_request.result;size_t n=strlen(text_request.text);if(n>=(size_t)cap){n=cap-1;while(n&&((unsigned char)text_request.text[n]&0xc0)==0x80)n--;}
    memcpy(out,text_request.text,n);out[n]=0;pthread_mutex_unlock(&text_request.lock);return result;
}
typedef struct { const char *title,*sub,*guide,*initial; int max,cap,result; char *out; } text_call;
static pthread_mutex_t text_call_lock=PTHREAD_MUTEX_INITIALIZER;
static void *text_input_worker(void *argument) {
    text_call *call=argument;call->result=text_input_request(call->title,call->sub,call->guide,call->initial,call->max,call->out,call->cap);return NULL;
}
int front_text_input(const char *title,const char *sub,const char *guide,const char *initial,int max,char *out,int cap) {
    // 客体线程换到客体栈后不能附着 ART；JNI 全部在有真实 pthread 栈元数据的宿主线程调用。
    // A guest thread running on its guest stack cannot attach to ART; perform all JNI calls on a host thread with genuine pthread stack metadata.
    pthread_mutex_lock(&text_call_lock);text_call call={title,sub,guide,initial,max,cap,-1,out};pthread_t thread;
    int error=pthread_create(&thread,NULL,text_input_worker,&call);if(!error)pthread_join(thread,NULL);
    pthread_mutex_unlock(&text_call_lock);return call.result;
}
int dm_thermal_now(void) {
    void *library=dlopen("libandroid.so",RTLD_NOW|RTLD_LOCAL);if(!library)return -1;
    void *(*acquire)(void)=dlsym(library,"AThermal_acquireManager");void (*release)(void*)=dlsym(library,"AThermal_releaseManager");int (*status)(void*)=dlsym(library,"AThermal_getCurrentThermalStatus");int result=-1;
    if(acquire&&release&&status){void *manager=acquire();if(manager){int v=status(manager);result=v<0?-1:v==0?0:v<=2?1:v==3?2:3;release(manager);}}dlclose(library);return result;
}
static void pad_publish(game_app *s) {
    int32_t x=s->lx,y=s->ly;
    if(s->move_keys){x=((!!(s->move_keys&DM_BTN_RIGHT))-(!!(s->move_keys&DM_BTN_LEFT)))*DM_STICK_MAX;y=((!!(s->move_keys&DM_BTN_UP))-(!!(s->move_keys&DM_BTN_DOWN)))*DM_STICK_MAX;}
    // 分开合并实体与屏幕手柄；抬起一只手不会清掉另一输入来源仍按住的键。
    // 十字键来自 HAT 轴时用它自己的位掩码，否则一次 HAT 归零会把同时按住的 DPAD 键码也清掉。
    // Merge physical and on-screen pads separately; releasing one source never clears a key held by another.
    // D-pad bits that arrive as HAT axes keep their own mask, otherwise one HAT zero would also clear a
    // simultaneously held DPAD keycode.
    if(!x&&!y){x=s->touch_lx;y=s->touch_ly;}
    int32_t rx=s->rx,ry=s->ry;if(!rx&&!ry){rx=s->touch_rx;ry=s->touch_ry;}
    dm_pad_set(s->pad_buttons|s->hat_buttons|s->trigger_buttons|s->touch_buttons|(s->tap_a?DM_BTN_A:0),x,y,rx,ry);
}
static void pad_clear(game_app *s) {s->pad_buttons=s->hat_buttons=s->trigger_buttons=s->move_keys=s->touch_buttons=0;s->tap_a=0;s->lx=s->ly=s->rx=s->ry=s->touch_lx=s->touch_ly=s->touch_rx=s->touch_ry=0;pad_publish(s);}
static int32_t touch_axis(jint value) {return value>DM_STICK_MAX?DM_STICK_MAX:value< -DM_STICK_MAX?-DM_STICK_MAX:value;}
// 游戏内菜单的两项运行时开关。注意命名方向：Java 侧的 setDebugOverlay 是 native → Java（通知显隐），
// 这两个是 Java → native（菜单点了要改状态），所以名字不同。
// Two runtime switches for the in-game menu. Mind the direction: on the Java side setDebugOverlay runs native ->
// Java (show/hide notification), these two run Java -> native (a menu tap changes state), hence the different names.
__attribute__((visibility("default"))) void Java_org_d2r_android_port_D2RActivity_setOverlayEnabled(JNIEnv *env,jclass klass,jboolean enabled) {
    (void)env;(void)klass;atomic_store(&debug_overlay_enabled,enabled?1:0);
}
__attribute__((visibility("default"))) void Java_org_d2r_android_port_D2RActivity_setQualityPreset(JNIEnv *env,jclass klass,jint preset) {
    (void)env;(void)klass;dm_3d_scale_set_preset((int)preset);
}
__attribute__((visibility("default"))) void Java_org_d2r_android_port_D2RActivity_setMenuOpen(JNIEnv *env,jclass klass,jboolean open) {
    (void)env;(void)klass;atomic_store(&menu_open,open?1:0);
}
__attribute__((visibility("default"))) void Java_org_d2r_android_port_D2RActivity_setSkipSaveOnExit(JNIEnv *env,jclass klass,jboolean skip) {
    (void)env;(void)klass;atomic_store(&skip_save_once,skip?1:0);
}
__attribute__((visibility("default"))) void Java_org_d2r_android_port_D2RActivity_onTouchPad(JNIEnv *env,jclass klass,jlong buttons,jint lx,jint ly,jint rx,jint ry) {
    (void)env;(void)klass;game_app *s=application;if(!s)return;
    // Java 窗口触控和 AInputQueue 都在 Activity 的主 Looper；统一走 HLE 的短按锁存。
    // Java window touches and AInputQueue run on the Activity's main Looper; both use the HLE press-edge latch.
    s->touch_buttons=(uint64_t)buttons&0xffff;s->touch_lx=touch_axis(lx);s->touch_ly=touch_axis(ly);s->touch_rx=touch_axis(rx);s->touch_ry=touch_axis(ry);pad_publish(s);
}
static int32_t stick_value(float value) {if(value>1)value=1;if(value<-1)value=-1;return value>-.15f&&value<.15f?0:(int32_t)(value*DM_STICK_MAX);}
static uint64_t pad_key(int key) {
    switch(key){case AKEYCODE_DPAD_UP:return DM_BTN_UP;case AKEYCODE_DPAD_DOWN:return DM_BTN_DOWN;case AKEYCODE_DPAD_LEFT:return DM_BTN_LEFT;case AKEYCODE_DPAD_RIGHT:return DM_BTN_RIGHT;
        case AKEYCODE_ENTER:case AKEYCODE_SPACE:case AKEYCODE_BUTTON_A:return DM_BTN_A;case AKEYCODE_ESCAPE:case AKEYCODE_BUTTON_B:return DM_BTN_B;
        case AKEYCODE_BUTTON_X:return DM_BTN_X;case AKEYCODE_BUTTON_Y:return DM_BTN_Y;case AKEYCODE_BUTTON_L1:return DM_BTN_L;case AKEYCODE_BUTTON_R1:return DM_BTN_R;
        case AKEYCODE_BUTTON_L2:return DM_BTN_ZL;case AKEYCODE_BUTTON_R2:return DM_BTN_ZR;case AKEYCODE_BUTTON_START:return DM_BTN_PLUS;case AKEYCODE_BUTTON_SELECT:return DM_BTN_MINUS;
        case AKEYCODE_BUTTON_THUMBL:return DM_BTN_LS;case AKEYCODE_BUTTON_THUMBR:return DM_BTN_RS;default:return 0;}
}
static int input_ready(int fd,int events,void *data) {
    (void)fd;(void)events;game_app *s=data;AInputEvent *event;
    while(AInputQueue_getEvent(s->input,&event)>=0){if(AInputQueue_preDispatchEvent(s->input,event))continue;int handled=0;
        // 手柄事件可能带 JOYSTICK 源，也可能只带 GAMEPAD 源；只认前者的话，纯 GAMEPAD 源的方向键会掉进
        // 下面的触摸分支，被当成一根手指的坐标。
        // Pad events arrive with either a JOYSTICK or a GAMEPAD source. Accepting only the former sends a
        // GAMEPAD-source D-pad into the touch branch below, where it is read as a finger position.
        int source=AInputEvent_getSource(event),is_motion=AInputEvent_getType(event)==AINPUT_EVENT_TYPE_MOTION;
        // 游戏内菜单打开时挡住手柄与按键：菜单是独立窗口，手柄事件照样从 AInputQueue 进来，不挡的话角色会在菜单后面
        // 继续动。两处**放行**：① 返回键 —— 它要走到菜单窗口去把菜单收起（菜单是 focusable 弹窗，自己会吃掉返回键）；
        // ② 触摸 —— 菜单自己的触摸走它自己的窗口，屏幕手柄也要能点。
        // While the in-game menu is open, pad and key events are swallowed here: the menu is a separate window and
        // pad events still arrive on this AInputQueue, so without this the character keeps moving behind the menu.
        // Two exceptions: ① the back key, which has to reach the menu window so it can be closed (a focusable popup
        // consumes it itself); ② touches, since the menu's touches go to its own window and the on-screen controls
        // must stay usable.
        if(atomic_load(&menu_open)&&((AInputEvent_getType(event)==AINPUT_EVENT_TYPE_KEY&&AKeyEvent_getKeyCode(event)!=AKEYCODE_BACK)||(is_motion&&(source&(AINPUT_SOURCE_JOYSTICK|AINPUT_SOURCE_GAMEPAD))))){AInputQueue_finishEvent(s->input,event,1);continue;}
        if(is_motion&&(source&(AINPUT_SOURCE_JOYSTICK|AINPUT_SOURCE_GAMEPAD))){
            s->lx=stick_value(AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_X,0));s->ly=stick_value(-AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_Y,0));
            float x=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_Z,0),y=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_RZ,0);if(!x)x=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_RX,0);if(!y)y=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_RY,0);s->rx=stick_value(x);s->ry=stick_value(-y);
            // 十字键：不少手柄（含 Xbox）把它报成 HAT 轴而不是 DPAD 键码，之前整块被丢掉，方向键就没反应。
            // HAT_Y 负为上、正为下，这是安卓的约定。阈值 ±0.5 容忍模拟量。
            // D-pad: many pads (Xbox included) report it as HAT axes rather than DPAD keycodes, and it used to be
            // dropped entirely, which is why the D-pad did nothing. HAT_Y is negative up and positive down by the
            // Android convention; the ±0.5 threshold tolerates analogue hats.
            float hx=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_HAT_X,0),hy=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_HAT_Y,0);
            uint64_t hat=dm_pad_hat_buttons(hx,hy);
            // 只在变化时记一行：方向键本来就是低频事件，这样出问题时能直接看到"哪一次按下的方向被认成了什么"。
            // Log only on change: the D-pad is a low-frequency event, and when it misbehaves this shows exactly
            // which press was recognised as which direction.
            if(hat!=s->hat_buttons)LOG("[android-pad] hat x=%.2f y=%.2f → buttons=%#llx",hx,hy,(unsigned long long)hat);
            s->hat_buttons=hat;
            // 扳机（ZL/ZR）：Xbox 手柄把 LT/RT 报成模拟轴而不是 L2/R2 键码（见 dm_input.h），所以键码分支收不到，
            // 必须在动这里读。AXIS_LTRIGGER/RTRIGGER 是主名字，部分手柄只给 AXIS_BRAKE/GAS 别名，作为兜底。
            // Triggers (ZL/ZR): an Xbox pad reports LT/RT as analogue axes rather than L2/R2 keycodes (see
            // dm_input.h), so the keycode branch never sees them and they have to be read here. AXIS_LTRIGGER /
            // RTRIGGER are the primary names; some pads only expose the AXIS_BRAKE / GAS aliases, read as a fallback.
            float lt=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_LTRIGGER,0);if(!lt)lt=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_BRAKE,0);
            float rt=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_RTRIGGER,0);if(!rt)rt=AMotionEvent_getAxisValue(event,AMOTION_EVENT_AXIS_GAS,0);
            uint64_t trigger=dm_pad_trigger_buttons(lt,rt);
            // 同样只在变化时记：出问题时能直接看到扳机按到多少才算按下（阈值在 dm_input.h 里，一处可调）。
            // Logged on change as well, so a misbehaving trigger shows exactly how far it was pressed; the threshold
            // lives in dm_input.h and is the single place to tune.
            if(trigger!=s->trigger_buttons)LOG("[android-pad] trigger LT=%.2f RT=%.2f → buttons=%#llx (阈值 %.2f)",lt,rt,(unsigned long long)trigger,0.5);
            s->trigger_buttons=trigger;
            pad_publish(s);handled=1;
        }else if(is_motion){int raw_action=AMotionEvent_getAction(event),action=raw_action&AMOTION_EVENT_ACTION_MASK;unsigned lifted=(raw_action&AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)>>AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;dm_touch touches[16];int count=0;
            pthread_mutex_lock(&s->lock);ANativeWindow *window=s->window;int w=window?ANativeWindow_getWidth(window):1,h=window?ANativeWindow_getHeight(window):1;pthread_mutex_unlock(&s->lock);
            float gw=w,gh=w*9.f/16.f;if(gh>h){gh=h;gw=h*16.f/9.f;}float left=(w-gw)/2,top=(h-gh)/2;
            if(action!=AMOTION_EVENT_ACTION_UP&&action!=AMOTION_EVENT_ACTION_CANCEL)for(unsigned i=0;i<AMotionEvent_getPointerCount(event)&&count<16;i++){if(action==AMOTION_EVENT_ACTION_POINTER_UP&&i==lifted)continue;float x=(AMotionEvent_getX(event,i)-left)/gw,y=(AMotionEvent_getY(event,i)-top)/gh;if(x>=0&&x<1&&y>=0&&y<1)touches[count++]=(dm_touch){AMotionEvent_getPointerId(event,i)&15,x,y};}
            dm_touch_set(touches,count);
            // 首画面阶段：点击可确认标题/提示；按下沿由 HLE 锁存，不丢短按。
            // First-frame controls: taps confirm title/prompts; HLE latches press edges so short taps are retained.
            if(action==AMOTION_EVENT_ACTION_DOWN)s->tap_a=1;else if(action==AMOTION_EVENT_ACTION_UP||action==AMOTION_EVENT_ACTION_CANCEL)s->tap_a=0;pad_publish(s);handled=1;
        }else if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_KEY){int key=AKeyEvent_getKeyCode(event),down=AKeyEvent_getAction(event)==AKEY_EVENT_ACTION_DOWN;uint64_t button=pad_key(key),move=key==AKEYCODE_W?DM_BTN_UP:key==AKEYCODE_A?DM_BTN_LEFT:key==AKEYCODE_S?DM_BTN_DOWN:key==AKEYCODE_D?DM_BTN_RIGHT:0;
            if(button){uint64_t before=s->pad_buttons;if(down)s->pad_buttons|=button;else s->pad_buttons&=~button;handled=1;
                // 按键码这条路也要看得见（LB/RB/A/B 等走这里，部分手柄的方向键也走这里）：出问题时能直接对上
                // 哪一颗键换来哪一位，不必靠猜。只在位掩码真的变了才记，连按不放不会刷屏。
                // The keycode path has to be visible too (LB/RB/A/B and, on some pads, the D-pad come through here):
                // when something misbehaves this shows which button produced which bit instead of leaving it to
                // guesswork. Logged only when the mask really changes, so holding a button does not flood the log.
                if(s->pad_buttons!=before)LOG("[android-pad] keycode=%d down=%d → buttons=%#llx",key,down,(unsigned long long)s->pad_buttons);}
            if(move){if(down)s->move_keys|=move;else s->move_keys&=~move;handled=1;}if(handled)pad_publish(s);}
        AInputQueue_finishEvent(s->input,event,handled);
    }return 1;
}
// 退出前请游戏存盘并等它提交，最多 3 秒。
// 游戏自己的定时存盘是 5 分钟一次，且没有"切后台就存"这条路：不请它，从上次提交到现在的进度就没了。
// Mac（front_mac.m 的 app_quit）与 iOS（front_ios.m 进后台）都做了这件事，安卓原先漏了。
// 没等到提交要如实记红，不能让人以为存上了。
// Ask the game to save and wait up to 3 s for the commit before shutting down. The game's own timed autosave is
// every 5 minutes and it has no save-on-background path, so without asking, everything since the last commit is
// lost. Mac (app_quit in front_mac.m) and iOS (going to background) already do this; Android did not. Failing to
// get a commit is logged loudly rather than left to look like a successful save.
static void settle_save(void) {
    // 刚导入过存档（游戏内菜单）：这次退出不写盘，否则旧的内存状态会把导入的存档盖掉。
    // A save was just imported (from the in-game menu): skip writing on this exit, otherwise the stale in-memory
    // state would overwrite the imported save.
    if(atomic_exchange(&skip_save_once,0)){LOG("[android] 跳过存盘：刚导入存档，本次退出不再写盘");return;}
    int dm_ingame_now(void);int dm_uiscreen_ingame(void);int dm_request_save(void);int dm_unpause_for_exit(void);uint64_t dm_fs_commit_count(void);uint64_t now_ns(void);
    if(!dm_ingame_now()&&!dm_uiscreen_ingame()){LOG("[android] 退出：不在局内，没有待存的进度");return;}
    // 暂停菜单/面板开着时游戏不跑拍，拨的那次存盘等不到，所以先放开暂停（不写回）。
    // With a pause menu or panel open the game does not tick, so the requested save never happens; release the
    // pause first (not written back).
    uint64_t c0=dm_fs_commit_count(),t0=now_ns(),last=c0,last_t=0;int asked=0,paused=0;
    for(;;){
        paused|=dm_unpause_for_exit();if(!asked)asked=dm_request_save();
        usleep(20000);uint64_t c=dm_fs_commit_count(),t=now_ns();
        if(c!=last){last=c;last_t=t;}
        if((asked&&last_t&&t-last_t>=200000000ull)||t-t0>=3000000000ull)break;
    }
    if(last!=c0&&asked)LOG("[android] 退出前等游戏存盘 %.0f ms，期间存档提交 %llu 次%s",(now_ns()-t0)/1e6,(unsigned long long)(last-c0),paused?"（原先停在暂停菜单/面板，已放开）":"");
    else LOG("[android] 【红】未存盘退出：3 秒内没等到存档提交（%s%s）",asked?"已请游戏存盘":"不处于可请存盘的状态",paused?"，原先暂停着、已放开":"");
}
static void window_created(ANativeActivity *a,ANativeWindow *w) {game_app *s=a->instance;pthread_mutex_lock(&s->lock);ANativeWindow_acquire(w);s->window=w;s->frame_rate_attempted=0;pthread_cond_signal(&s->ready);pthread_mutex_unlock(&s->lock);
    // 游戏已在跑时这块窗口是"从后台回来"的新窗口：挂起期间重建表面与交换链（此刻游戏线程被挡在 NVN 入口外，不会争）。
    // When the game is already running this is the new window from returning to the foreground: rebuild the surface
    // and swapchain while graphics are suspended, when the game thread is parked outside NVN and cannot contend.
    if(atomic_load(&s->started)){
        void mtl_screen_attach(struct ANativeWindow *);mtl_screen_attach(w);
        // mtl_screen_attach 自己先挂起（重建交换链时游戏线程不能在外面读它），但**不**放开：这个回调与
        // onResume 的先后由系统决定，真机实测是"图形恢复（onResume）先、窗口后"，所以迟到时必须在这里按
        // 当前是否在前台把状态摆回去；否则那次挂起没人解除，游戏会永远卡在出图上。
        // mtl_screen_attach suspends first (the game thread must not be reading the swapchain while it is
        // rebuilt) but does not release it: the order of this callback against onResume is the system's choice —
        // measured on device, 图形恢复 (onResume) comes first and the window after — so a late window has to put
        // the state back here, based on whether the app is in the foreground; otherwise nothing would release
        // that suspension and the game would stay stuck on its last frame.
        // 同时解除客体时钟冻结：旋转若走的是"销毁+重建窗口"这条路（有些设备/版本如此，见下面的 window_resized），
        // 那次 window_destroyed 里冻了时钟，而旋转不经过 onPause/onResume，没人来解冻，游戏会永远停拍。在前台拿到窗口
        // 就该是走着的：正常回前台那条路 onResume 已经解过，这里是幂等的空操作。
        // The guest clock freeze is released here too: if a rotation takes the "destroy + recreate the window" route
        // (some devices/versions do so; see window_resized below) that window_destroyed froze the clock, and a
        // rotation passes through neither onPause nor onResume, so nothing would unfreeze it and the game would stop
        // ticking forever. A window handed to us in the foreground means it should be running: on the normal
        // return-from-background path onResume already released it, so this is an idempotent no-op there.
        if(atomic_load(&s->activity_resumed)){dm_gfx_suspend(0);guest_clock_freeze_for(1,0);}}}
// 旋转或窗口尺寸变化（配置变化时安卓不重建窗口，只改尺寸，所以不会走 window_destroyed/created）：交换链是按建链时的
// 尺寸建的，不重建的话新画面会被合成器按旧尺寸拉伸或裁掉，于是这里复用「从后台回来」那一条重建路径 ——
// mtl_screen_attach 先挂起图形（游戏线程不能在外面读交换链）再按窗口的最新尺寸重建表面与交换链。
// 系统只在**同一个**窗口的宽或高变了时才发这个回调；换成新窗口走 onNativeWindowCreated（见 window_created）。
// 旋转不经过 onPause/onResume，且是瞬时的，所以**不**冻结客体时钟（那是进后台才做的），只按当前是否在前台放开挂起。
// A rotation or window-size change (Android does not rebuild the window for a configuration change, it only
// resizes it, so this never arrives as window_destroyed/created): the swapchain was built at the old size, and
// without rebuilding it the compositor stretches or crops the new frame, so this reuses the return-from-background
// rebuild -- mtl_screen_attach suspends graphics first (the game thread must not be reading the swapchain) and then
// rebuilds the surface and swapchain at the window's latest size. The system fires this only when the width or
// height of the *same* window changed; a new window goes through onNativeWindowCreated (see window_created). A
// rotation does not pass through onPause/onResume and is instantaneous, so the guest clock is **not** frozen (that
// is for going to the background); the suspension is released per the current foreground state.
static void window_resized(ANativeActivity *a,ANativeWindow *w) {game_app *s=a->instance;
    pthread_mutex_lock(&s->lock);int same_window=s->window==w;pthread_mutex_unlock(&s->lock);
    // 同一个窗口才处理：新窗口有 onNativeWindowCreated 那条路；后端还没活时也没有交换链要重建，第一次出图会自己建。
    // 重建这一拍会慢（表面/交换链重建、缓存变冷），照回前台那次一样先告诉动态分辨率控制器别把它当成 GPU 吃紧。
    // Only the same window is handled here: a new one goes through onNativeWindowCreated, and while the backend is
    // not live yet there is no swapchain to rebuild -- the first present builds it itself. The rebuilding frame is
    // slow (surface/swapchain rebuild, caches gone cold), so as on returning to the foreground the dynamic
    // resolution controller is told not to read it as GPU pressure.
    if(same_window&&atomic_load(&s->started)){void mtl_screen_attach(struct ANativeWindow *);mtl_screen_attach(w);dm_3d_scale_resumed();if(atomic_load(&s->activity_resumed))dm_gfx_suspend(0);LOG("[android] 窗口尺寸变化：表面与交换链已按新尺寸重建，进程与本局保留");}}
static void window_destroyed(ANativeActivity *a,ANativeWindow *w) {game_app *s=a->instance;
    // 窗口销毁不再等于退出进程：安卓在 Activity 停止时必然销毁窗口，保留进程才能在回到前台时接着玩。
    // 退出前照 Mac/iOS 请游戏存盘（否则丢掉上一次提交之后的进度），再释放表面与交换链；真正退出走 destroyed()。
    // **顺序不能换**：请存盘在前、冻结客体时钟与挂起图形在后。反过来游戏线程会被挡在 NVN 入口外，那次自动存盘
    // 永远提交不了，切后台就等于没存 —— 真机上就是这么失败的。
    // 客体时钟一并冻结（iOS 同一处理）：游戏线程挡在提交处、墙钟却照走，不冻结的话回来时游戏内时间会一下跳过
    // 整段后台时长（补拍 / 动画 / 冷却全跟着跳）。真机上曾看到"整帧 83839.6 ms"，就是那一段。
    // **The order matters**: the save request comes first; freezing the guest clock and suspending graphics come
    // after. The other way round parks the game thread outside NVN, that autosave can never commit, and going to
    // the background saves nothing — which is exactly how it failed on device.
    // The guest clock is frozen as well (same as iOS): the game thread is blocked at submission while the wall clock
    // keeps running, so without the freeze in-game time jumps over the whole background duration on return
    // (catch-up ticks / animations / cooldowns all jump with it). An "83,839.6 ms whole frame" was seen on device.
    // A destroyed window no longer means the process ends: Android always destroys it when the activity stops, and
    // keeping the process is what lets play resume. Save first as Mac and iOS do (otherwise everything since the
    // last commit is lost), then release the surface and swapchain. A real exit goes through destroyed().
    if(atomic_load(&s->started)){settle_save();guest_clock_freeze_for(1,1);void mtl_screen_detach(void);mtl_screen_detach();LOG("[android] 窗口销毁：保留进程等待回到前台");}
    pthread_mutex_lock(&s->lock);if(s->window==w){s->window=NULL;ANativeWindow_release(w);}pthread_mutex_unlock(&s->lock);}
static void input_created(ANativeActivity *a,AInputQueue *q) {game_app *s=a->instance;s->input=q;AInputQueue_attachLooper(q,ALooper_forThread(),0,input_ready,s);}
static void input_destroyed(ANativeActivity *a,AInputQueue *q) {game_app *s=a->instance;AInputQueue_detachLooper(q);s->input=NULL;dm_touch_set(NULL,0);pad_clear(s);}
// 注意 onPause 里**不**挂起图形（Mac 的最小化路径有这一步，安卓不能照抄）：安卓在停下来之后一定会销毁窗口，
// 而"请游戏存盘"排在窗口销毁之前（见 window_destroyed）。图形一旦在这里挂起，游戏线程就被挡在 NVN 入口外，
// 请求的那次自动存盘永远等不到提交 —— 真机上就是这样，日志里只剩一条"未存盘退出"的红字。真正的挂起由
// window_destroyed 的 detach 做，那时机在请存盘之后；而只要窗口销毁了，也就没有出图可做了。
// Note that onPause does **not** suspend graphics (Mac's minimize path does; Android must not copy that): Android
// always destroys the window once stopped, and the "ask the game to save" step is ordered before that destroy (see
// window_destroyed). Suspending here parks the game thread outside NVN, so the requested autosave can never commit
// — which is exactly what happened on device, leaving only a red "exited without saving" line. The real suspension
// is done by detach in window_destroyed, after the save request; and once the window is gone there is no presenting
// to do anyway.
// 回前台这边另外照 iOS 补了两件事：① dm_3d_scale_resumed——回来头几秒的慢帧是恢复（表面重建、缓存变冷），
// 不告诉控制器就会被当成 GPU 吃紧、把 3D 内部比例降下去；② 解除客体时钟冻结，与进后台时那次冻结配对。
// Returning to the foreground also mirrors two things iOS does: ① dm_3d_scale_resumed — the slow frames in the
// first seconds back are recovery (surface rebuild, cold caches), and without telling the controller they are read
// as GPU pressure and the internal 3D ratio is scaled down; ② lifting the guest clock freeze, pairing with the
// freeze taken when going to the background.
static void resumed(ANativeActivity *a) {game_app *s=a->instance;atomic_store(&s->activity_resumed,1);dm_android_performance_resume();if(atomic_load(&s->started)){dm_3d_scale_resumed();dm_gfx_suspend(0);guest_clock_freeze_for(1,0);dm_audio_suspend(0);dm_focus_set(1);}}
static void paused(ANativeActivity *a) {game_app *s=a->instance;atomic_store(&s->activity_resumed,0);dm_touch_set(NULL,0);pad_clear(s);if(atomic_load(&s->started)){dm_focus_set(3);dm_audio_suspend(1);}dm_android_performance_close();}
static void destroyed(ANativeActivity *a) {game_app *s=a->instance;atomic_store(&s->debug_stop,1);
    // 请存盘只做一次：正常退出时 window_destroyed 一定先跑过（安卓在 Activity 停止时必然先销毁窗口），而且那时
    // 图形还没挂起、游戏跑得动，提交等得到。走到这里图形已经挂起，再请一次根本等不到提交，只会白等 3 秒再打一条
    // 假的红色报警（真机上正常退出就是这样多出一条红字）。所以只有窗口还在（window_destroyed 没跑过）时才在这里请。
    // Ask for a save only once: on a normal exit window_destroyed has always run first (Android always destroys the
    // window when the activity stops), and back then graphics were not yet suspended so the game could make progress
    // and the commit could arrive. Here graphics are already suspended, so asking again cannot produce a commit — it
    // would only waste three seconds and log a false red alarm (which is exactly what a normal exit did on device).
    // Ask here only when the window is still present, i.e. window_destroyed never ran.
    pthread_mutex_lock(&s->lock);int window_present=s->window!=NULL;pthread_mutex_unlock(&s->lock);
    if(atomic_load(&s->started)){if(window_present)settle_save();dm_host_shutdown("Android activity destroyed");}
    pthread_mutex_lock(&s->lock);s->stop=1;pthread_cond_signal(&s->ready);pthread_mutex_unlock(&s->lock);pthread_join(s->worker,NULL);if(s->debug_started)pthread_join(s->debug_thread,NULL);if(s->window)ANativeWindow_release(s->window);free(s);application=NULL;}
__attribute__((visibility("default"))) void ANativeActivity_onCreate(ANativeActivity *a,void *saved,size_t bytes) {
    // NativeActivity 自行 dlopen，不登记 Java 类加载器的 native 库；显式注册文本与触控回调。
    // NativeActivity dlopens its library without Java class-loader native-library registration; explicitly register text and touch callbacks.
    jclass klass=(*a->env)->GetObjectClass(a->env,a->clazz);JNINativeMethod methods[]={
        {"onTextResult","(JLjava/lang/String;Z)V",(void*)Java_org_d2r_android_port_D2RActivity_onTextResult},
        {"onTouchPad","(JIIII)V",(void*)Java_org_d2r_android_port_D2RActivity_onTouchPad},
        {"setOverlayEnabled","(Z)V",(void*)Java_org_d2r_android_port_D2RActivity_setOverlayEnabled},
        {"setQualityPreset","(I)V",(void*)Java_org_d2r_android_port_D2RActivity_setQualityPreset},
        {"setMenuOpen","(Z)V",(void*)Java_org_d2r_android_port_D2RActivity_setMenuOpen},
        {"setSkipSaveOnExit","(Z)V",(void*)Java_org_d2r_android_port_D2RActivity_setSkipSaveOnExit}
    };
    if((*a->env)->RegisterNatives(a->env,klass,methods,sizeof methods/sizeof methods[0])!=JNI_OK){(*a->env)->ExceptionDescribe(a->env);(*a->env)->ExceptionClear(a->env);ANativeActivity_finish(a);return;}(*a->env)->DeleteLocalRef(a->env,klass);
    (void)saved;(void)bytes;game_app *s=calloc(1,sizeof *s);if(!s){ANativeActivity_finish(a);return;}s->activity=a;a->instance=s;application=s;pthread_mutex_init(&s->lock,NULL);pthread_cond_init(&s->ready,NULL);
    a->callbacks->onNativeWindowCreated=window_created;a->callbacks->onNativeWindowResized=window_resized;a->callbacks->onNativeWindowDestroyed=window_destroyed;a->callbacks->onInputQueueCreated=input_created;a->callbacks->onInputQueueDestroyed=input_destroyed;a->callbacks->onPause=paused;a->callbacks->onResume=resumed;a->callbacks->onDestroy=destroyed;
    ANativeActivity_setWindowFlags(a,AWINDOW_FLAG_FULLSCREEN|AWINDOW_FLAG_KEEP_SCREEN_ON,0);pthread_attr_t attr;pthread_attr_init(&attr);pthread_attr_setstacksize(&attr,64u<<20);int error=pthread_create(&s->worker,&attr,boot,s);pthread_attr_destroy(&attr);if(error)ANativeActivity_finish(a);
}
