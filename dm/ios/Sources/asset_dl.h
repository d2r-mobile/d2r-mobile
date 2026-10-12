// 直装包的游戏资源下载（实现与下载站约定见 asset_dl.m）
// Game asset download for the direct-install build (implementation and the download-site conventions: see asset_dl.m)
#import <Foundation/Foundation.h>
@interface DmAssetDL : NSObject
+ (instancetype)shared;
+ (instancetype)astcPack;   // 老机型补充包：下到 Documents/Legacy Device Pack/data（地址见 asset_dl.m 文件头） | Legacy Device Pack: downloaded to Documents/Legacy Device Pack/data (address: see the asset_dl.m file header)
// 定下载地址、取清单、盘点本地已有的。回调在主线程：err 非空 = 走不了直接下载（原因）；remain / total = 还要下 / 总字节数；cellular = 当前是按流量计费的网络
// Determine the download address, fetch the manifest, take stock of what is already local. Callback on the main thread: err non-nil = direct download not possible (reason); remain / total = bytes still to download / total bytes; cellular = the current network is metered
- (void)prepare:(void (^)(NSString *err, int64_t remain, int64_t total, BOOL cellular))done;
// 上一次 prepare 判出手机空间不够时：还差多少字节（含 200 MB 余量）与手机可用字节；空间够 = 0
// When the last prepare found the phone short of space: how many bytes are still missing (including a 200 MB margin) and the phone's available bytes; enough space = 0
@property (atomic) int64_t shortBytes, freeBytes;
// 开始（或接着）下载。progress(已到手字节数, 总字节数, 附注或 nil) 与 done(失败原因或 nil) 都在主线程
// Start (or resume) downloading. progress(bytes received, total bytes, note or nil) and done(failure reason or nil) are both on the main thread
- (void)run:(void (^)(int64_t done, int64_t total, NSString *note))progress done:(void (^)(NSString *err))done;
@end
void dm_asset_dl_bg_events(NSString *ident, void (^done)(void));   // App 委托的 handleEventsForBackgroundURLSession 转来（按会话标识分给资源 / 补充包） | Forwarded from the App delegate's handleEventsForBackgroundURLSession (dispatched to assets / Legacy Device Pack by session identifier)
