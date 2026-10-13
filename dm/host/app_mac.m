// macOS App 形态（双击起）：宿主被包成 <名>.app 时，启动最前面替玩家把命令行那套准备好——
//   窗口开着；存档放 ~/Library/Application Support/D2R Mobile/save（与开发用的工作区、测试存档都不相干）；
//   游戏资源文件夹第一次启动时请玩家选（记在偏好里，下次不再问；选的不是资源文件夹给明确报错、重选）；
//   程序文件（改写后的游戏程序，产物目录）随包放在 Contents/Resources/product；日志写到 ~/Library/Logs/D2R Mobile/host.log。
// 命令行起（闸门、截图、脚本）不走这里：不在 .app 包里，或带了参数，一律原样。打包脚本 dm/tools/mac_app.sh。
// macOS App form (started by double-click): when the host is packaged as <name>.app, the very first thing at startup is to prepare the command-line setup on the player's behalf --
//   the window is on; saves go to ~/Library/Application Support/D2R Mobile/save (unrelated to the development workspace and to test saves);
//   the game resource folder is chosen by the player on first launch (remembered in preferences and not asked again; choosing something that is not a resource folder gives a clear error and another try);
//   the program files (the rewritten game program, the build output directory) are in Contents/Resources/product inside the package; the log is written to ~/Library/Logs/D2R Mobile/host.log.
// Command-line starts (gates, screenshots, scripts) do not come here: not inside an .app package, or with arguments, everything stays as it was. Packaging script: dm/tools/mac_app.sh.
#import <Cocoa/Cocoa.h>
#include <sys/stat.h>
#include "hle.h"
#include "dm_i18n.h"
#define T(k) (@(dm_ui(k)))

#define APP_NAME "D2R Mobile"
static NSString *const kRomfsKey = @"RomfsDir";

// 这个文件夹是不是游戏资源：认 data/build_manifest.psv。选到上一两层（解包目录的根、Update）也认，往里找一层
// Whether this folder is the game resources: recognized by data/build_manifest.psv. Choosing one or two levels up (the root of the unpacked directory, Update) is accepted too, looking one level in
static NSString *romfs_root(NSString *dir, NSString **why) {
    NSFileManager *fm = NSFileManager.defaultManager;
    for (NSString *sub in @[@"", @"RomFS", @"romfs", @"Update/RomFS"]) {
        NSString *d = sub.length ? [dir stringByAppendingPathComponent:sub] : dir;
        if ([fm fileExistsAtPath:[d stringByAppendingPathComponent:@"data/build_manifest.psv"]]) {
            if (![fm isReadableFileAtPath:[d stringByAppendingPathComponent:@"data/build_manifest.psv"]]) { if (why) *why = T("dmAppNoRead"); return nil; }
            return d;
        }
    }
    if (why) *why = [NSString stringWithFormat:T("dmAppNotRomfs"), dir.lastPathComponent.UTF8String];
    return nil;
}
static void alert(NSString *title, NSString *text) {
    NSAlert *a = [NSAlert new]; a.messageText = title; a.informativeText = text; [a addButtonWithTitle:T("dmAppOK")]; [a runModal];
}
// 请玩家选资源文件夹，直到选对或取消。返回资源根目录，取消返回 nil
// Ask the player to choose the resource folder until it is right or cancelled. Returns the resource root, or nil on cancel
static NSString *pick_romfs(NSString *first_why) {
    NSString *why = first_why;
    for (;;) {
        NSOpenPanel *p = [NSOpenPanel openPanel];
        p.canChooseDirectories = YES; p.canChooseFiles = NO; p.allowsMultipleSelection = NO; p.prompt = T("dmAppPickUse");
        p.message = why ? [NSString stringWithFormat:T("dmAppPickAgain"), why.UTF8String] : T("dmAppPickMsg");
        if ([p runModal] != NSModalResponseOK) return nil;
        NSString *w = nil, *r = romfs_root(p.URL.path, &w);
        if (r) return r;
        alert(T("dmAppNotRomfsTitle"), w); why = w;
    }
}

// 包里的一项（product / msl / romfs_overlay）：实物在 Contents/Resources/<名> 就用它；没有就看 Resources/paths.txt 里「名=本机路径」那一行
// （mac_app.sh 的「不带派生物」出法：改写后的游戏程序、着色器库、界面覆盖层都从游戏内容派生，公开发布的包不带，指向本机自己构建出来的）
// One item of the package (product / msl / romfs_overlay): if the real thing is in Contents/Resources/<name> it is used; otherwise the line "name=local path" in Resources/paths.txt is consulted
// (the "without derived content" build of mac_app.sh: the rewritten game program, the shader library and the UI overlay are all derived from game content, a publicly released package does not carry them and points at what this machine built itself)
static NSString *res_item(NSString *name) {
    NSFileManager *fm = NSFileManager.defaultManager; NSString *rp = NSBundle.mainBundle.resourcePath, *d = [rp stringByAppendingPathComponent:name];
    if ([fm fileExistsAtPath:d]) return d;
    NSString *txt = [NSString stringWithContentsOfFile:[rp stringByAppendingPathComponent:@"paths.txt"] encoding:NSUTF8StringEncoding error:nil];
    for (NSString *ln in [txt componentsSeparatedByString:@"\n"]) { NSString *pre = [name stringByAppendingString:@"="]; if ([ln hasPrefix:pre]) { d = [ln substringFromIndex:pre.length]; return [fm fileExistsAtPath:d] ? d : nil; } }
    return nil;
}
// main() 最前面调。是 App 形态就把环境与参数备好并返回 1；不是返回 0、什么都不动
// Called at the very start of main(). In App form it prepares the environment and arguments and returns 1; otherwise it returns 0 and touches nothing
int dm_app_bootstrap(int *argc, char ***argv) {
    @autoreleasepool {
        NSString *bp = NSBundle.mainBundle.bundlePath;
        if (*argc > 1 || ![bp.pathExtension isEqualToString:@"app"]) return 0;
        NSFileManager *fm = NSFileManager.defaultManager;
        if (!getenv("DM_LANG")) setenv("DM_LANG", [NSLocale.preferredLanguages.firstObject hasPrefix:@"zh"] ? "zh-Hans" : "en", 1);   // 命令行宿主一律简中；App 形态跟系统首选语言（提示框与游戏内同一个开关） | The command-line host is always Simplified Chinese; the App form follows the system's preferred language (dialogs and in-game text use the same switch)
        NSString *sup = [[NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@APP_NAME] copy];
        if (getenv("DM_APP_SUPPORT")) sup = @(getenv("DM_APP_SUPPORT"));   // 测试：存档等放到指定目录，不碰玩家那份 | Test: put saves and the rest in a given directory, leaving the player's own untouched
        NSString *logd = [[NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES).firstObject stringByAppendingPathComponent:@"Logs/" APP_NAME] copy];
        NSString *save = [sup stringByAppendingPathComponent:@"save"], *temp = [sup stringByAppendingPathComponent:@"temp"];
        for (NSString *d in @[sup, logd, save, temp]) [fm createDirectoryAtPath:d withIntermediateDirectories:YES attributes:nil error:nil];
        // 双击起没有终端：日志落文件，上一次的留一份
        // A double-click start has no terminal: the log goes to a file, and the previous one is kept
        NSString *lf = [logd stringByAppendingPathComponent:@"host.log"];
        [fm removeItemAtPath:[logd stringByAppendingPathComponent:@"host.prev.log"] error:nil];
        [fm moveItemAtPath:lf toPath:[logd stringByAppendingPathComponent:@"host.prev.log"] error:nil];
        if (!isatty(2)) { freopen(lf.fileSystemRepresentation, "w", stderr); setvbuf(stderr, NULL, _IOLBF, 0); }
        [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular]; [NSApp activateIgnoringOtherApps:YES];
        NSString *prod = res_item(@"product");
        if (!prod || ![fm fileExistsAtPath:[prod stringByAppendingPathComponent:@"main.patched.bin"]]) {
            alert(T("dmAppBrokenTitle"), T("dmAppBroken")); exit(1);
        }
        NSUserDefaults *ud = NSUserDefaults.standardUserDefaults;
        NSString *saved = [ud stringForKey:kRomfsKey], *why = nil, *rom = saved ? romfs_root(saved, &why) : nil;
        if (getenv("DM_APP_ROMFS")) {   // 测试（没人点对话框时用）：直接给资源文件夹；不对就把同一句报错写进日志并退出，不弹框、不记进偏好 | Test (for when nobody is there to click the dialog): give the resource folder directly; if it is wrong, write the same error sentence to the log and exit, with no dialog and nothing saved to preferences
            rom = romfs_root(@(getenv("DM_APP_ROMFS")), &why);
            if (!rom) { LOG("【红】App：DM_APP_ROMFS 给的不是游戏资源文件夹：%s", why.UTF8String); exit(2); }
        } else if (!rom) {
            rom = pick_romfs(saved ? [NSString stringWithFormat:T("dmAppRomfsGone"), why.UTF8String] : nil);
            if (!rom) { LOG("App：没有选资源文件夹，退出"); exit(0); }
            [ud setObject:rom forKey:kRomfsKey];
        }
        { void dm_custom_init(const char *dir); dm_custom_init(sup.fileSystemRepresentation); }   // 自定义设置文件的目录现在就给（同 iOS 壳）：不给的话要等存档挂上才知道在哪，而语言开关正是在挂存档那一刻读它改游戏语言的——晚了开关就不生效 | the directory of the custom settings file is given right now (same as the iOS shell): otherwise it is only known once the save is mounted, and the language switch is read to change the game language at exactly that moment - too late and the switch has no effect
        setenv("DM_WINDOW", "1", 1); setenv("DM_APP", "1", 1);
        setenv("DM_SAVE", save.fileSystemRepresentation, 1); setenv("DM_TEMP", temp.fileSystemRepresentation, 1);
        setenv("DM_ROMFS", rom.fileSystemRepresentation, 1); setenv("DM_ROOT", sup.fileSystemRepresentation, 1);
        NSString *shmap = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"shader_map.tsv"];   // 图形后端的着色器映射表：命令行起时按可执行文件旁的源码树找，包里放在 Resources | The graphics backend's shader mapping table: when started from the command line it is found via the source tree next to the executable; in the package it is in Resources
        if ([fm fileExistsAtPath:shmap]) setenv("DM_NVN_SHMAP", shmap.fileSystemRepresentation, 1);
        NSString *msl = res_item(@"msl");   // 编好的着色器库（msl/b-<翻译链版本>/） | The compiled shader library (msl/b-<translation chain version>/)
        if (msl) setenv("DM_MSL_DIR", msl.fileSystemRepresentation, 1);
        NSString *ov = res_item(@"romfs_overlay");
        if (ov) setenv("DM_ROMFS_OVERLAY", ov.fileSystemRepresentation, 1);
        static char *av[5]; av[0] = (*argv)[0]; av[1] = strdup(prod.fileSystemRepresentation); av[2] = "main"; av[3] = "sdk"; av[4] = NULL;
        *argv = av; *argc = 4;
        LOG("App：存档 %s；资源 %s；程序文件 %s；界面覆盖层 %s；日志 %s", save.UTF8String, rom.UTF8String, prod.UTF8String, getenv("DM_ROMFS_OVERLAY") ?: "（没有，原版界面）", lf.UTF8String);
        return 1;
    }
}

// 手柄代码（ios/Sources/dm_gamepad.m，两边共用）问「现在是不是面板 / 读取画面开着」：只用来决定空着的 ZR 格要不要当免费回城。
// 宿主没有 iOS 壳那套面板栈；一律答「是」= ZR 整颗交给原版（面板里它是翻页签键），手柄上的免费回城这一版没有（记在已知不足里）
// The gamepad code (ios/Sources/dm_gamepad.m, shared by both sides) asks "is a panel / loading screen open right now": used only to decide whether the empty ZR slot should act as the free town portal.
// The host has no panel stack like the iOS shell; it always answers "yes" = ZR is handed entirely to the original game (in panels it is the tab-switch key), so the free town portal on the gamepad is absent in this version (listed under known gaps)
int dm_front_navigation(void) { return 1; }
