// iCloud 存档同步的 iOS 一侧：拿 iCloud 容器目录、让云端的包先下载好，再调共用核心 dm_cloud_sync（dm/host/dm_cloudsync.c）。
// 启动：dm_cloud_boot 在 boot_thread 里、dm_main 挂存档之前调，最多等 5 秒，拉云端各包、按「后写覆盖先写」换进本地存档，之后游戏照常读存档列表。
// 运行中：存档每提交成功一次（hle_fs.c 的 Commit），提交钩子先把已提交目录 clonefile 成快照（写时复制，近乎零成本）；局内只记成待传，出局 / 切后台时在串行队列里把快照与云端对比、只上传（allow_down = 0）；不在局内的提交当场传。
// 只同步玩家真实存档 Documents/save；测试存档（save.test 等）缺省不同步。测试开关 DM_TEST_ICLOUD_SUB=<子目录名>：测试存档也同步，云端用容器里的这个子目录（与真实存档的 saves 隔开，不会把测试角色传到玩家设备上）。
// 测试开关 DM_TEST_ICLOUD_DIR=<本地目录>：用本地目录当云端（不碰 iCloud），测试存档也同步。不以 / 开头的路径拼到 App 的 Documents 下（下同）。
// 测试开关 DM_TEST_ICLOUD_ROOT=<本地目录>：用本地目录当 iCloud 容器的 Documents（下有 saves / saves_v2），验目录换代与一次性迁移；测试存档也同步。
// 开关：「自定义」页 icloud（缺省开）。没登录 iCloud / 本 App 的 iCloud 被关：取不到容器，整套跳过，日志记一行。
// iOS side of iCloud save sync: get the iCloud container directory, let the cloud packages download first, then call the shared core dm_cloud_sync (dm/host/dm_cloudsync.c).
// Startup: dm_cloud_boot is called in boot_thread before dm_main mounts the save; waits at most 5 seconds, pulls each cloud package and swaps it into the local save under "later write overwrites earlier write"; after that the game reads the save list as usual.
// While running: each time a save commit succeeds (Commit in hle_fs.c), the commit hook first clonefiles the committed directory into a snapshot (copy-on-write, nearly free); in a run it is only marked pending, and on leaving the run / going to background the snapshot is compared with the cloud on a serial queue, upload only (allow_down = 0); commits outside a run are uploaded on the spot.
// Only the player's real save Documents/save is synced; test saves (save.test etc.) are not synced by default. Test switch DM_TEST_ICLOUD_SUB=<subdirectory name>: test saves are synced too, and the cloud uses this subdirectory of the container (kept apart from the real save's saves, so test characters are not uploaded to the player's devices).
// Test switch DM_TEST_ICLOUD_DIR=<local directory>: use a local directory as the cloud (iCloud untouched); test saves are synced too. Paths not starting with / are joined under the App's Documents (same below).
// Test switch DM_TEST_ICLOUD_ROOT=<local directory>: use a local directory as the iCloud container's Documents (containing saves / saves_v2), to verify the directory generation change and the one-time migration; test saves are synced too.
// Switch: icloud on the 「自定义」(Custom) page (on by default). Not signed in to iCloud / iCloud turned off for this App: the container cannot be obtained, the whole thing is skipped and one log line is written.
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <signal.h>
#include <sys/clonefile.h>
#include <stdatomic.h>
#include "front.h"
#include "hle.h"

int dm_cloud_sync(const char *save, const char *cloud, const char *bak, const char *statef, int allow_down, uint64_t now_ms, int (*ready)(const char *unit));
int dm_custom_get(const char *key);
extern void (*g_dm_save_commit_hook)(const char *commit_dir);

static NSString *g_cloud, *g_save, *g_bak, *g_state;   // 云端目录 / 本地存档 / 备份 / 状态文件 | Cloud directory / local save / backup / state file
static dispatch_queue_t g_q;
static uint64_t now_ms(void) { return (uint64_t)([NSDate date].timeIntervalSince1970 * 1000); }

// 云端某个单位的包是否已在本机（iCloud 里没下载的文件是占位：旧系统是「.<名>.icloud」，新系统是同名但下载状态不是「最新」）
// Whether a given unit's package in the cloud is already on this device (undownloaded files in iCloud are placeholders: on older systems ".<name>.icloud", on newer systems the same name with a download status other than "current")
static int pack_ready(const char *unit) {
    NSString *nm = [NSString stringWithFormat:@"%s.pack", unit];
    if ([NSFileManager.defaultManager fileExistsAtPath:[g_cloud stringByAppendingPathComponent:[NSString stringWithFormat:@".%@.icloud", nm]]]) return 0;
    NSURL *u = [NSURL fileURLWithPath:[g_cloud stringByAppendingPathComponent:nm]]; NSString *st = nil;
    if (![u getResourceValue:&st forKey:NSURLUbiquitousItemDownloadingStatusKey error:nil] || !st) return 1;   // 不是 iCloud 管的文件（或没有这个包）：按已在本机 | Not an iCloud-managed file (or no such package): treated as already on this device
    return [st isEqualToString:NSURLUbiquitousItemDownloadingStatusCurrent];
}
static int pending_downloads(int start) {   // 还没下载好的包数；start = 1 时顺手请系统开始下载 | Number of packages not yet downloaded; with start = 1 also ask the system to start downloading
    int n = 0; NSFileManager *fm = NSFileManager.defaultManager;
    for (NSString *f in [fm contentsOfDirectoryAtPath:g_cloud error:nil] ?: @[]) {
        NSString *nm = f; if ([f hasPrefix:@"."] && [f hasSuffix:@".icloud"]) nm = [f substringWithRange:NSMakeRange(1, f.length - 8)];
        if (![nm hasSuffix:@".pack"]) continue;
        if (pack_ready([nm substringToIndex:nm.length - 5].UTF8String)) continue;
        n++; if (start) [fm startDownloadingUbiquitousItemAtURL:[NSURL fileURLWithPath:[g_cloud stringByAppendingPathComponent:nm]] error:nil];
    }
    return n;
}
static int run_sync(NSString *save, int down) {   // 在文件协调里做：iCloud 守护进程同时在写这个目录时不会读到半个文件 | Done inside file coordination: half-written files are not read while the iCloud daemon is writing this directory at the same time
    __block int r = -1; NSError *err = nil;
    [[[NSFileCoordinator alloc] initWithFilePresenter:nil] coordinateWritingItemAtURL:[NSURL fileURLWithPath:g_cloud] options:NSFileCoordinatorWritingForMerging error:&err byAccessor:^(NSURL *u) {
        (void)u; r = dm_cloud_sync(save.fileSystemRepresentation, g_cloud.fileSystemRepresentation, g_bak.fileSystemRepresentation, g_state.fileSystemRepresentation, down, now_ms(), pack_ready); }];
    if (err) LOG("【红】iCloud 同步：文件协调失败 %s", err.localizedDescription.UTF8String);
    return r;
}
// 存档提交钩子（hle_fs.c，持着存档锁调）：只做快照，其余放队列。
// 局内（58 分钟提交 127 次、上传 97 次，平均 35 秒碰一次网）：只留最新那份快照记成「待传」，不上传；
// 退出这一局回到前端（主菜单 / 选角页）、切到后台时各传一次（dm_cloud_flush）。不在局内（选角 / 建角 / 删角色）的提交照旧当场传——
// 删角色只在选角页发生，拖到以后传的话被强杀后再启动会把删掉的角色从云端拉回来（启动同步把「本地没了」当丢失、从云端拿回）。
// 被强杀 / 崩溃没来得及传：待传快照随之作废（启动时清掉），启动那次同步看到本地自上次同步后改过、云端没变，照常补传（dm_cloudsync.c lchg && !cchg → UP）
// Save commit hook (hle_fs.c, called while holding the save lock): only takes a snapshot, the rest goes to the queue.
// In a run (127 commits and 97 uploads in 58 minutes, touching the network every 35 seconds on average): keep only the newest snapshot marked as "pending upload", do not upload;
// upload once each when leaving the run back to the front end (main menu / character select page) and when going to background (dm_cloud_flush). Commits outside a run (character select / create / delete character) are still uploaded on the spot --
// character deletion only happens on the character select page; if deferred, a force-kill followed by a relaunch would pull the deleted character back from the cloud (startup sync treats "missing locally" as lost and restores it from the cloud).
// Force-killed / crashed before uploading: the pending snapshot is voided with it (cleared at startup), and the startup sync sees local changed since the last sync and cloud unchanged, so it uploads as usual (dm_cloudsync.c lchg && !cchg → UP)
static NSString *g_pend;          // 局内最新一次提交的快照（只在 g_q 上读写） | Snapshot of the newest commit in the run (read and written only on g_q)
static _Atomic int g_bg;          // App 在后台：此时的提交（失活时请求的那次存盘）当场传 | App in background: a commit at this time (the save requested on resign active) is uploaded on the spot
static _Atomic int g_session;     // 在一局里：进局（第一次出现局内画面）起，到游戏发「回到前端」（FrontEndNavigation:ToFrontEnd）为止——中间的读取画面（切幕 / 传送点）不算出局 | In a run: from entering the run (first appearance of the in-run picture) until the game sends "back to front end" (FrontEndNavigation:ToFrontEnd) -- loading screens in between (act change / waypoint) do not count as leaving the run
static void upload_snap(NSString *snap, const char *why) {   // 在 g_q 上调 | Called on g_q
    if (dm_custom_get("icloud")) { int n = run_sync(snap, 0); LOG("iCloud 同步：%s上传 %d 个", why, n); }
    [NSFileManager.defaultManager removeItemAtPath:snap error:nil];
}
static void on_commit(const char *commit_dir) {
    static unsigned seq; NSString *snap = [NSString stringWithFormat:@"%@.cloudsnap%u", g_save, ++seq];
    [NSFileManager.defaultManager removeItemAtPath:snap error:nil];
    if (clonefile(commit_dir, snap.fileSystemRepresentation, 0)) { LOG("【红】iCloud 同步：存档快照失败 errno=%d，这次提交不上传", errno); return; }
    int defer = atomic_load(&g_session) && !atomic_load(&g_bg);
    dispatch_async(g_q, ^{
        if (defer) { if (g_pend) [NSFileManager.defaultManager removeItemAtPath:g_pend error:nil]; g_pend = snap; return; }   // 局内：换成这一份，等出局 / 切后台 | In a run: replace with this copy and wait for leaving the run / going to background
        if (g_pend) { [NSFileManager.defaultManager removeItemAtPath:g_pend error:nil]; g_pend = nil; }   // 这一份更新，旧的待传作废 | This copy is newer; the old pending upload is voided
        upload_snap(snap, atomic_load(&g_bg) ? "后台存档提交后" : "存档提交后（不在局内）");
    });
}
// 出局 / 切后台：把局内攒下的最新一份传掉。切后台用系统给的后台收尾时间，传完才结束（最多约 30 秒，系统到点前会叫停）
// Leaving a run / going to background: upload the newest copy accumulated during the run. Going to background uses the background completion time granted by the system and ends only after the upload finishes (about 30 seconds at most; the system calls a stop before time is up)
void dm_cloud_flush(int background) {
    if (!g_q) return;   // 没开同步（测试存档 / 没登录 iCloud / 自定义页关了） | Sync not enabled (test save / not signed in to iCloud / turned off on the Custom page)
    if (background) atomic_store(&g_bg, 1);
    __block UIBackgroundTaskIdentifier tk = UIBackgroundTaskInvalid;
    if (background) tk = [UIApplication.sharedApplication beginBackgroundTaskWithName:@"dm.cloud" expirationHandler:^{ LOG("【红】iCloud 同步：后台收尾时间到，系统叫停（没传完的下次启动补传）"); [UIApplication.sharedApplication endBackgroundTask:tk]; tk = UIBackgroundTaskInvalid; }];
    dispatch_async(g_q, ^{
        NSString *p = g_pend; g_pend = nil;
        if (p) upload_snap(p, background ? "切到后台，" : "退出这一局回到前端，");
        else LOG("iCloud 同步：%s，局内没有待传的存档", background ? "切到后台" : "退出这一局回到前端");
        if (tk != UIBackgroundTaskInvalid) dispatch_async(dispatch_get_main_queue(), ^{ if (tk != UIBackgroundTaskInvalid) { [UIApplication.sharedApplication endBackgroundTask:tk]; tk = UIBackgroundTaskInvalid; } });
    });
}
void dm_cloud_foreground(void) { atomic_store(&g_bg, 0); }
static void cloud_ingame(int on) { if (on) atomic_store(&g_session, 1); }
static void cloud_charsel(void) { if (atomic_exchange(&g_session, 0)) dm_cloud_flush(0); }   // 退出这一局回到前端（主菜单 / 选角页）：这一局结束 | Left this run back to the front end (main menu / character select page): this run has ended

static NSURL *container(double limit) {   // 取 iCloud 容器：要在主线程以外调，第一次可能要等系统建目录；取不到返回 nil 并记原因 | Get the iCloud container: must be called off the main thread; the first time may have to wait for the system to create the directory; returns nil and logs the reason if it cannot be obtained
    __block NSURL *ct = nil; dispatch_semaphore_t got = dispatch_semaphore_create(0);
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{ ct = [NSFileManager.defaultManager URLForUbiquityContainerIdentifier:nil]; dispatch_semaphore_signal(got); });
    if (dispatch_semaphore_wait(got, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(limit * NSEC_PER_SEC)))) { LOG("iCloud 不可用：取容器超过 %.0f 秒，本次只用本地存档", limit); return nil; }
    if (!ct) LOG("iCloud 不可用：没登录 iCloud 或本 App 的 iCloud 被关，只用本地存档");
    return ct;
}

// 云端目录换代（编年史仓库第 5 步；旧版本是覆盖更新的）：本版起存档格式变了（储藏箱 14 列），旧包读新档会静默删掉新列里的物品。
// 云端改用 Documents/saves_v2，旧包仍用 Documents/saves，两边互不覆盖。新包第一次启动（saves_v2 还没有）把 saves 复制过去一次——只读旧目录，旧目录一个字节都不改、不删：
//   先等旧目录里的包全部下载到本机（占位文件复制过去是空的）；逐个文件复制到 saves_v2.tmp，最后写完成清单 sw_migrated.txt（迁移时的全部包名），
//   再把 saves_v2.tmp 整个改名成 saves_v2——改名是最后一步，有 saves_v2 就一定是写完清单的整套。中途被杀：下次启动先删掉残留的 .tmp 从头再来。
//   没等到下载完 / 复制失败 = 本次不迁移也不同步，下次启动再试（先同步会建出空的 saves_v2，之后再也不迁移，别的设备在旧目录里的新档就漏了）。
// 用之前查齐（所有设备，含迁移的那台）：saves_v2 里有清单、清单上的包都到了本机目录（已下载或是 iCloud 占位）才同步。另一台设备上 iCloud 是一个文件一个文件
// 下来的：没查齐就同步，本机较旧的那份会按「云端没有包 → 上传」传上去，后写覆盖掉迁移过来的新进度。查齐过一次就在本地记一笔（<存档>.cloudv2ok），以后不再查
// （之后角色删了包会少，不能一直按清单卡）。返回 0 = 本次跳过同步
// Cloud directory generation change (chronicle vault step 5; older versions were updated by overwrite): from this version the save format changed (stash has 14 columns); an old build reading a new save silently deletes the items in the new columns.
// The cloud now uses Documents/saves_v2 while old builds keep using Documents/saves, so neither overwrites the other. On its first launch (saves_v2 does not exist yet) the new build copies saves over once -- the old directory is only read, not one byte of it is changed or deleted:
//   first wait until all packages in the old directory are downloaded to this device (a placeholder file would copy over as empty); copy file by file into saves_v2.tmp, and finally write the completion manifest sw_migrated.txt (all package names at migration time),
//   then rename saves_v2.tmp as a whole to saves_v2 -- the rename is the last step, so if saves_v2 exists it is always a complete set with the manifest written. Killed midway: on the next launch delete the leftover .tmp first and start over.
//   Download not finished in time / copy failed = no migration and no sync this time, retry on the next launch (syncing first would create an empty saves_v2, after which migration never happens again and new saves from other devices in the old directory would be missed).
// Check completeness before use (all devices, including the one that migrated): sync only when saves_v2 contains the manifest and every package on the manifest has reached this device's directory (downloaded, or present as an iCloud placeholder). On another device iCloud brings files down one
// at a time: syncing before the check passes, this device's older copy would be uploaded under "no package in the cloud → upload", and the later write would overwrite the newer migrated progress. Once the check has passed, record it locally (<save>.cloudv2ok) and never check again
// (later, deleting a character removes packages, so we cannot keep blocking on the manifest forever). Returns 0 = skip sync this time
static NSString *test_path(const char *e) {   // 测试开关里的路径：不以 / 开头的拼到 App 的 Documents 下（真机容器路径每次装包都变） | Paths in test switches: those not starting with / are joined under the App's Documents (the real-device container path changes on every install)
    if (!e) return nil; if (e[0] == '/') return @(e);
    return [NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@(e)];
}
static int v2_ready(NSString *nw, NSString *okf) {   // 迁移过来的整套是否都到了本机 | Whether the whole migrated set has arrived on this device
    NSFileManager *fm = NSFileManager.defaultManager; if ([fm fileExistsAtPath:okf]) return 1;
    NSString *ls = [NSString stringWithContentsOfFile:[nw stringByAppendingPathComponent:@"sw_migrated.txt"] encoding:NSUTF8StringEncoding error:nil];
    if (!ls) { LOG("iCloud 同步：saves_v2 里还没有完成清单（别的设备的迁移还没传完？），本次不同步"); return 0; }
    int miss = 0, n = 0; for (NSString *f in [ls componentsSeparatedByString:@"\n"]) { if (!f.length) continue; n++;
        if (![fm fileExistsAtPath:[nw stringByAppendingPathComponent:f]] && ![fm fileExistsAtPath:[nw stringByAppendingPathComponent:[NSString stringWithFormat:@".%@.icloud", f]]]) miss++; }
    if (miss) { LOG("iCloud 同步：saves_v2 清单 %d 项还缺 %d 项没到本机，本次不同步", n, miss); return 0; }
    [@"" writeToFile:okf atomically:YES encoding:NSUTF8StringEncoding error:nil]; LOG("iCloud 同步：saves_v2 清单 %d 项都已到本机", n); return 1;
}
static int migrate_v2(NSString *docs, double deadline) {
    NSFileManager *fm = NSFileManager.defaultManager; NSString *nw = [docs stringByAppendingPathComponent:@"saves_v2"], *old = [docs stringByAppendingPathComponent:@"saves"], *tmp = [nw stringByAppendingString:@".tmp"];
    if ([fm fileExistsAtPath:nw]) return 1;
    if (![fm fileExistsAtPath:old]) { LOG("iCloud 同步：云端没有旧目录 saves，直接用 saves_v2"); [fm createDirectoryAtPath:nw withIntermediateDirectories:YES attributes:nil error:nil];
        [@"" writeToFile:[nw stringByAppendingPathComponent:@"sw_migrated.txt"] atomically:YES encoding:NSUTF8StringEncoding error:nil]; return 1; }   // 空清单：没有要等的包 | Empty manifest: no packages to wait for
    g_cloud = old; int pend = pending_downloads(1);
    while (pend && CFAbsoluteTimeGetCurrent() < deadline) { usleep(100000); pend = pending_downloads(0); }
    g_cloud = nil;
    if (pend) { LOG("iCloud 同步：旧目录 saves 还有 %d 个包没下载完，本次不迁移、不同步，下次启动再试", pend); return 0; }
    const char *kill_at = getenv("DM_TEST_ICLOUD_MIGRATE_KILL");   // 测试：复制完第 N 个文件后自杀（N = marker：写完清单、改名之前），验中途被杀 | Test: kill self after copying the Nth file (N = marker: after writing the manifest, before the rename), to verify being killed midway
    __block BOOL ok = YES; __block NSError *e2 = nil; __block NSMutableString *list = [NSMutableString string]; __block int copied = 0; NSError *err = nil;
    [[[NSFileCoordinator alloc] initWithFilePresenter:nil] coordinateReadingItemAtURL:[NSURL fileURLWithPath:old] options:0 error:&err byAccessor:^(NSURL *u) {
        if ([fm fileExistsAtPath:tmp]) { LOG("iCloud 同步：上次迁移没做完（有 saves_v2.tmp），删掉重来"); [fm removeItemAtPath:tmp error:nil]; }
        ok = [fm createDirectoryAtPath:tmp withIntermediateDirectories:NO attributes:nil error:&e2];
        for (NSString *f in [fm contentsOfDirectoryAtPath:u.path error:nil] ?: @[]) { if (!ok) break; if ([f hasPrefix:@"."]) continue;   // 隐藏文件（系统的）不带 | Hidden files (the system's) are not carried over
            ok = [fm copyItemAtPath:[u.path stringByAppendingPathComponent:f] toPath:[tmp stringByAppendingPathComponent:f] error:&e2]; if (!ok) break;
            if ([f hasSuffix:@".pack"]) [list appendFormat:@"%@\n", f]; copied++;
            if (kill_at && atoi(kill_at) == copied) { LOG("DM_TEST_ICLOUD_MIGRATE_KILL：复制完第 %d 个文件，自杀", copied); kill(getpid(), SIGKILL); } }
        if (ok) ok = [list writeToFile:[tmp stringByAppendingPathComponent:@"sw_migrated.txt"] atomically:YES encoding:NSUTF8StringEncoding error:&e2];
        if (ok && kill_at && !strcmp(kill_at, "marker")) { LOG("DM_TEST_ICLOUD_MIGRATE_KILL：清单已写、改名之前自杀"); kill(getpid(), SIGKILL); }
        if (ok) ok = [fm moveItemAtPath:tmp toPath:nw error:&e2]; }];
    if (!ok || err) { [fm removeItemAtPath:tmp error:nil];
        if ([fm fileExistsAtPath:nw]) { LOG("iCloud 同步：saves_v2 已由别的设备建好，用它"); return 1; }   // 两台同时迁：后改名的那台失败，用先到的那套 | Two devices migrating at once: the one that renames later fails and uses the set that arrived first
        LOG("【红】iCloud 同步：旧目录 saves 复制到 saves_v2 失败（%s），本次不同步", (err ?: e2).localizedDescription.UTF8String ?: "?"); return 0; }
    LOG("iCloud 同步：旧目录 saves 已复制到 saves_v2（%d 个文件，清单 %lu 个包），旧目录留给旧包用", copied, (unsigned long)[list componentsSeparatedByString:@"\n"].count - 1); return 1;
}
void dm_cloud_boot(void) {
    const char *sv = getenv("DM_SAVE"), *sub = getenv("DM_TEST_ICLOUD_SUB"), *ldir = getenv("DM_TEST_ICLOUD_DIR"), *lroot = getenv("DM_TEST_ICLOUD_ROOT"); if (!sv) return;
    NSString *doc = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
    NSString *save = @(sv); int real = [save.stringByStandardizingPath isEqualToString:[doc stringByAppendingPathComponent:@"save"].stringByStandardizingPath];
    int off = getenv("DM_TEST_ICLOUD_OFF") != NULL;                   // 测试总闸：脚本起的局一律带它（tools/cloud_guard.sh），整局不同步 | Test master gate: runs launched by scripts always carry it (tools/cloud_guard.sh), and the whole run does not sync
    if (!real && !sub && !ldir && !lroot) { if (off) LOG("iCloud 同步：测试开关 DM_TEST_ICLOUD_OFF，本局不同步"); return; }   // 测试存档：不同步 | Test save: not synced
    // Mac 上跑的 iOS 包：缺省整个不连真实 iCloud。这个包只发 iPhone / iPad，Mac 上跑的全是开发与测试；开发机登着真实账号时，
    // 它就是那个账号的又一台设备，任何脚本、手点图标都会和真实云端双向同步（出过把测试存档传进真实云端）。本地测试云端（DIR / ROOT）不受影响；
    // 真要在 Mac 上连真实云端：同时给 DM_REAL_ICLOUD=1 与 DM_REAL_ICLOUD_CONFIRM=写入真实云端。真机不走这一支。
    // iOS app running on a Mac: by default it does not connect to real iCloud at all. This package ships only for iPhone / iPad, and everything run on a Mac is development and testing; when the development machine is signed in to a real account,
    // it is one more device of that account, and any script or a tap on the icon would sync both ways with the real cloud (test saves have been uploaded to the real cloud before). The local test cloud (DIR / ROOT) is unaffected;
    // to really connect to the real cloud on a Mac: give both DM_REAL_ICLOUD=1 and DM_REAL_ICLOUD_CONFIRM=写入真实云端. Real devices do not take this branch.
    if (NSProcessInfo.processInfo.isiOSAppOnMac && !ldir && !lroot) { const char *r1 = getenv("DM_REAL_ICLOUD"), *r2 = getenv("DM_REAL_ICLOUD_CONFIRM");
        if (!(r1 && !strcmp(r1, "1") && r2 && !strcmp(r2, "写入真实云端"))) { LOG("iCloud 同步：Mac 上运行：缺省不同步 iCloud"); return; }
        LOG("【红】iCloud 同步：Mac 上运行，已显式放开（DM_REAL_ICLOUD）：本局连真实云端"); }
    if (off) { LOG("iCloud 同步：测试开关 DM_TEST_ICLOUD_OFF，本局不同步"); return; }
    if (!dm_custom_get("icloud")) { LOG("iCloud 同步：自定义页已关，不同步"); return; }
    double t0 = CFAbsoluteTimeGetCurrent(), limit = 5.0;
    if (ldir) g_cloud = test_path(ldir);                             // 测试：本地目录当云端（没有 iCloud 权限的开发包也能验整条链） | Test: use a local directory as the cloud (a dev build without iCloud entitlement can also verify the whole chain)
    else if (sub) { NSURL *ct = container(limit); if (!ct) return; g_cloud = [[ct.path stringByAppendingPathComponent:@"Documents"] stringByAppendingPathComponent:@(sub)]; }
    else { NSString *docs = test_path(lroot); if (!lroot) { NSURL *ct = container(limit); if (!ct) return; docs = [ct.path stringByAppendingPathComponent:@"Documents"]; }
        if (!migrate_v2(docs, t0 + limit)) return;
        NSString *nw = [docs stringByAppendingPathComponent:@"saves_v2"]; if (!v2_ready(nw, [save stringByAppendingString:@".cloudv2ok"])) return;
        g_cloud = nw; }
    [NSFileManager.defaultManager createDirectoryAtPath:g_cloud withIntermediateDirectories:YES attributes:nil error:nil];
    g_save = save; g_bak = [save stringByAppendingString:@".cloudbak"]; g_state = [save stringByAppendingString:@".cloudstate"];
    if (ldir) { g_bak = [g_cloud stringByAppendingString:@".cloudbak"]; g_state = [g_cloud stringByAppendingString:@".cloudstate"]; }   // 测试的本地云端：状态与备份跟云端目录走，不与真存档（同一路径、连真实 iCloud 时）共用——共用的话测试局会改写真存档的同步状态 | Local cloud for tests: state and backup follow the cloud directory and are not shared with the real save (same path, when connected to real iCloud) -- if shared, test runs would rewrite the real save's sync state
    g_q = dispatch_queue_create("dm.cloud", DISPATCH_QUEUE_SERIAL);
    for (NSString *f in [NSFileManager.defaultManager contentsOfDirectoryAtPath:save.stringByDeletingLastPathComponent error:nil] ?: @[])   // 上次被杀时没删掉的提交快照 | Commit snapshots not deleted when we were last killed
        if ([f hasPrefix:[save.lastPathComponent stringByAppendingString:@".cloudsnap"]]) [NSFileManager.defaultManager removeItemAtPath:[save.stringByDeletingLastPathComponent stringByAppendingPathComponent:f] error:nil];
    int pend = pending_downloads(1);
    while (pend && CFAbsoluteTimeGetCurrent() - t0 < limit) { usleep(100000); pend = pending_downloads(0); }
    int n = run_sync(save, 1);
    LOG("iCloud 同步：启动 %.0f ms，动作 %d 个%s（云端目录 %s）", (CFAbsoluteTimeGetCurrent() - t0) * 1000, n, pend ? [NSString stringWithFormat:@"，%d 个包没等到下载完、本次跳过", pend].UTF8String : "", ldir ? "本地测试目录" : sub ? sub : "saves_v2");
    g_dm_save_commit_hook = on_commit;
    { extern void (*g_dm_ingame_hook)(int); extern void (*g_dm_charsel_hook)(void); g_dm_ingame_hook = cloud_ingame; g_dm_charsel_hook = cloud_charsel; }   // 进局记一局开始；回到前端 = 这一局结束，传一次 | Entering a run marks the start of a run; returning to the front end = this run has ended, upload once
}
