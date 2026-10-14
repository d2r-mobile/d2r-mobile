// 出图裁切回归：彩色8×8纹理，闭式RGBA期望；真实Metal渲染，不依赖游戏或跨局确定性。
// Present crop regression: colored 8×8 texture, closed-form RGBA expectations; real Metal rendering, no dependence on the game or on cross-run determinism.
#include "../nvn/nvn_mtl.m"
#include "../nvn/hle_nvn.c"
uint64_t g_t0_ns;   // 宿主 main.c 的进程起点（hle_nvn 逐帧 @t_s 用），单测替身 | process start time from host main.c (used by hle_nvn's per-frame @t_s); unit test stub
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t g_mod[MAX_MOD]; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
#ifdef CROP_LEGACY_HEADER
#include CROP_LEGACY_HEADER
#define ENCODE legacy_present_encode
#else
#define ENCODE present_encode
#endif
static int failed, cases;
#define EXPECT(n, w, v) do { int a=(w),b=(v); cases++; printf("%s 期望 %d → 真结果 %d %s\n",n,a,b,a==b?"✓":"✗");failed+=a!=b; } while(0)
static const uint8_t colors[4][4]={{255,0,0,255},{0,255,0,255},{0,0,255,255},{255,255,0,255}};

static void check(id<MTLTexture> src, const char *name, const int crop[4], int w, int h, int color) {
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:w height:h mipmapped:NO];
    d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageRenderTarget;
    id<MTLTexture> dst=[g_dev newTextureWithDescriptor:d];id<MTLCommandBuffer> cb=[g_q commandBuffer];
    EXPECT("编码成功",1,ENCODE(cb,src,dst,crop));[cb commit];[cb waitUntilCompleted];
    EXPECT("GPU完成",MTLCommandBufferStatusCompleted,cb.status);
    uint8_t *out=malloc(w*h*4);[dst getBytes:out bytesPerRow:w*4 fromRegion:MTLRegionMake2D(0,0,w,h) mipmapLevel:0];
    int bad=0;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++) {
        int q=color;
        if(q==4) { // 正方形有效区对2:1目标：两侧各4列黑，中间8列全红。 | square valid area onto a 2:1 target: 4 black columns on each side, the middle 8 columns all red.
            static const uint8_t black[4]={0,0,0,255};
            const uint8_t *want=x<4||x>=12?black:colors[0];bad+=memcmp(out+4*(y*w+x),want,4)!=0;continue;
        }
        if(q==-2) { // crop=(1,1,4,4)：3×3红、末列绿、末行蓝、右下1格黄。 | crop=(1,1,4,4): 3×3 red, last column green, last row blue, bottom-right 1 cell yellow.
            q=(x+1>=4)+2*(y+1>=4);
        } else if(q<0) { // 同尺寸默认整帧：分界在正中。 | same-size default whole frame: the boundary is dead center.
            q=(x>=w/2)+2*(y>=h/2);
        }
        bad+=memcmp(out+4*(y*w+x),colors[q],4)!=0;
    }
    EXPECT(name,0,bad);free(out);
}

int main(void) { @autoreleasepool {
    mtl_init();
    MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:8 height:8 mipmapped:NO];d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;
    id<MTLTexture> src=[g_dev newTextureWithDescriptor:d];uint8_t px[8*8*4];
    for(int y=0;y<8;y++)for(int x=0;x<8;x++)memcpy(px+4*(y*8+x),colors[(x>=4)+2*(y>=4)],4);
    [src replaceRegion:MTLRegionMake2D(0,0,8,8) mipmapLevel:0 withBytes:px bytesPerRow:32];
    int zero[4]={0},tl[4]={0,0,4,4},tr[4]={4,0,4,4},bl[4]={0,4,4,4},br[4]={4,4,4,4},one[4]={3,3,1,1},inset[4]={1,1,4,4};
    check(src,"默认整帧64像素",zero,8,8,-1);
    check(src,"NULL默认整帧64像素",NULL,8,8,-1);
    check(src,"左上裁切放大256像素全红",tl,16,16,0);
    check(src,"非零x裁切放大256像素全绿",tr,16,16,1);
    check(src,"非零y裁切放大256像素全蓝",bl,16,16,2);
    check(src,"右下裁切贴边256像素全黄",br,16,16,3);
    check(src,"单纹素放大不跨边界256像素全红",one,16,16,0);
    check(src,"非零双偏移16像素逐字节等手写子矩形",inset,4,4,-2);
    check(src,"有效区比例保留，128像素中间红/两侧黑",tl,16,8,4);
    int invalid[4][4]={{-1,0,4,4},{0,-1,4,4},{0,0,0,4},{7,0,4,4}};
    for(int i=0;i<4;i++) {
        MTLTextureDescriptor *dd=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:8 height:8 mipmapped:NO];dd.usage=MTLTextureUsageRenderTarget;dd.storageMode=MTLStorageModeShared;
        id<MTLTexture> dst=[g_dev newTextureWithDescriptor:dd];
        EXPECT("无效crop不编码、不把整张拉伸冒充成功",0,ENCODE([g_q commandBuffer],src,dst,invalid[i]));
    }
    // 真实QueuePresent入口：先设crop入队，再立即改window，出图票号必须保留当帧副本。
    // Real QueuePresent entry: set crop and enqueue first, then immediately change the window; the present ticket must keep that frame's copy.
    PFNNVNDEVICEGETPROCADDRESSPROC get=(PFNNVNDEVICEGETPROCADDRESSPROC)h_nvnBootstrapLoader("nvnDeviceGetProcAddress");
    NVNT_nvnWindowSetCrop set=(NVNT_nvnWindowSetCrop)get(NULL,"nvnWindowSetCrop");
    NVNT_nvnQueuePresentTexture present=(NVNT_nvnQueuePresentTexture)get(NULL,"nvnQueuePresentTexture");
    tex_t tex={.serial=10001};tex.d.width=tex.d.height=8;tex.d.depth=tex.d.levels=1;tex.d.format=NVN_FORMAT_RGBA8;tex.d.target=NVN_TEXTURE_TARGET_2D;serial_put(tex.serial,src);
    win_t win={.ntex=1};win.tex[0]=(NVNtexture *)&tex;set((NVNwindow *)&win,4,4,4,4);
    mtl_present_test_stall(100000000);present(NULL,(NVNwindow *)&win,0);set((NVNwindow *)&win,0,0,8,8);uint64_t tk=x_present_ticket(win.ticket[0]);   // ticket 存翻译队列任务号，换成出图线程票号 | ticket holds the translation queue's job number; swap it for the present thread's ticket
#ifdef CROP_LEGACY_HEADER
    EXPECT("旧队列结构没有四参数副本空间（结构检查）",1,sizeof(pq_item)>=sizeof(tex_t)+sizeof(double)+sizeof(uint64_t)+sizeof br);
#else
    pthread_mutex_lock(&g_pq_mu);int saved=memcmp(g_pq[(tk-1)%PQ].crop,br,sizeof br)==0;pthread_mutex_unlock(&g_pq_mu);
    EXPECT("真实QueuePresent当帧crop副本不随window变化",1,saved);
#endif
    mtl_present_wait(tk);mtl_present_test_stall(0);
    printf("== %d 项，失败 %d ==\n",cases,failed);return failed!=0;
} }
