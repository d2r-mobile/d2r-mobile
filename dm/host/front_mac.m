// Mac 前端：DM_WINDOW=1 时开窗口（CAMetalLayer，供图形后端出图）+ 键盘 → dm_input；客体改在大栈线程上跑，主线程归 AppKit。
// 不开 DM_WINDOW：客体直接在当前线程跑，行为与无前端时相同（无窗口测试用这个）。
// 键位（任天堂布局）：WASD 左摇杆、方向键十字键、J=A K=B U=X I=Y、Q=L E=R、Z=ZL C=ZR、F=按左摇杆 G=按右摇杆、回车=+ Tab=−。
// Mac front end: with DM_WINDOW=1 open a window (CAMetalLayer, for the graphics backend to present frames) + keyboard → dm_input; the guest runs on a large-stack thread instead and the main thread belongs to AppKit.
// Without DM_WINDOW: the guest runs directly on the current thread, behaving the same as with no front end (windowless tests use this).
// Key map (Nintendo layout): WASD left stick, arrow keys D-pad, J=A K=B U=X I=Y, Q=L E=R, Z=ZL C=ZR, F=press left stick G=press right stick, Return=+ Tab=−.
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#import <Metal/Metal.h>
#include "dm.h"
#include "dm_input.h"
#include "dm_uiscreen.h"
void mtl_set_ui_rect_fn(void (*fn)(double *y, double *h));
static void ui_rect_frac(double *fy, double *fh) { int x, y, w, h, rw = 0, rh = 0; dm_ui_rect(&x, &y, &w, &h); if (dm_screen_real(&rw, &rh) && rh > 0) { *fy = (double)y / rh; *fh = (double)h / rh; } }   // 同 front_ios.m：界面矩形的纵向位置与高（占画面高的比例） | Same as front_ios.m: vertical position and height of the UI rectangle (as fractions of the frame height)
__attribute__((constructor)) static void ui_shift_reg(void) { mtl_set_ui_rect_fn(ui_rect_frac); }   // 不开窗口的跑法（截图 / 闸门）也要生效，所以不放在建图层那里 | Must also take effect when running without a window (screenshots / gate checks), so it is not placed where the layer is created
#include "front.h"
#include "hle.h"
#include <signal.h>

void dm_touch_host_live_enable(void); void dm_touch_host_live(int op, float nx, float ny);   // touch_mac.c
static int g_live;   // App 形态（或 DM_TOUCH_LIVE=1）：鼠标进我们的触屏状态机；否则照旧当原版触摸屏的一个触点 | App form (or DM_TOUCH_LIVE=1): the mouse goes into our touch state machine; otherwise it stays a single touch point of the original touch screen as before
static CAMetalLayer *g_layer;
static NSView *g_view;
// 窗口不是 16:9（拉伸窗口、全屏到 16:10 的屏）：游戏画面保持 16:9 居中，四周留黑边；鼠标位置按游戏画面这一块算
// The window is not 16:9 (stretched window, full screen on a 16:10 display): the game picture stays 16:9 and centred with black bars around it; the mouse position is computed against the game picture
static NSRect game_rect(void) {
    NSSize b = g_view.bounds.size; CGFloat w = b.width, h = b.height;
    if (w * 9 > h * 16) w = floor(h * 16 / 9); else h = floor(w * 9 / 16);
    return NSMakeRect(floor((b.width - w) / 2), floor((b.height - h) / 2), w, h);
}
static void layout_game(void) {
    NSRect r = game_rect(); if (NSEqualRects(r, g_layer.frame)) return;
    [CATransaction begin]; [CATransaction setDisableActions:YES]; g_layer.frame = r; g_layer.contentsScale = g_view.window.backingScaleFactor; [CATransaction commit];
    LOG("窗口：内容区 %.0f×%.0f，游戏画面 %.0f×%.0f 放在 (%.0f,%.0f)", g_view.bounds.size.width, g_view.bounds.size.height, r.size.width, r.size.height, r.origin.x, r.origin.y);
}
// 退出（关窗口 / ⌘Q / 菜单）：有局在就先请游戏走一次它自己的存盘并等存档提交（做法同 iOS 壳进后台），最多 3 秒，再有序收尾。
// 暂停菜单 / 大秘境面板开着时游戏停拍：先放开暂停让拍走起来；读取画面里等它进到局内再请。3 秒到了还没有提交就照实记「未存盘退出」
// Quit (closing the window / ⌘Q / menu): if a game is in progress, first ask the game to do its own save and wait for the save to be committed (the same way the iOS shell does when going to the background), for at most 3 seconds, then shut down in order.
// With the pause menu / rift panel open the game is not ticking: release the pause first so the ticks run; in a loading screen wait until it is in the game, then ask. If nothing is committed when the 3 seconds are up, truthfully log "quit without saving"
static void app_quit(const char *why) {
    static int once; if (once++) return;
    int dm_ingame_now(void); int dm_request_save(void); int dm_unpause_for_exit(void); uint64_t dm_fs_commit_count(void); uint64_t now_ns(void);
    if (!dm_ingame_now() && !dm_uiscreen_ingame()) { LOG("窗口：退出（不在局内，没有待存的进度）"); dm_host_shutdown(why); return; }
    uint64_t c0 = dm_fs_commit_count(), t0 = now_ns(), last = c0, last_t = 0; int asked = 0, paused = 0;
    for (;;) {
        paused |= dm_unpause_for_exit(); if (!asked) asked = dm_request_save();
        usleep(20000); uint64_t c = dm_fs_commit_count(), t = now_ns();
        if (c != last) { last = c; last_t = t; }
        if ((asked && last_t && t - last_t >= 200000000ull) || t - t0 >= 3000000000ull) break;
    }
    if (last != c0 && asked) LOG("窗口：退出前等游戏存盘 %.0f ms，期间存档提交 %llu 次%s", (now_ns() - t0) / 1e6, (unsigned long long)(last - c0), paused ? "（原先停在暂停菜单 / 面板，已放开）" : "");
    else LOG("【红】窗口：未存盘退出——3 秒内没等到存档提交（%s%s）", asked ? "已请游戏存盘" : "一直没到能请存盘的状态（读取画面？）", paused ? "，原先暂停着、已放开" : "");
    dm_host_shutdown(why);
}
static const char *ui(const char *k) { const char *dm_ui(const char *key); return dm_ui(k); }
static void show_keys(void) { NSAlert *a = [NSAlert new]; a.messageText = @(ui("dmAppKeysTitle")); a.informativeText = @(ui("dmAppKeysText")); [a addButtonWithTitle:@(ui("dmAppOK"))]; [a runModal]; }
@interface DmWinCtl : NSObject <NSWindowDelegate> @end
@implementation DmWinCtl
- (BOOL)windowShouldClose:(NSWindow *)w { (void)w; app_quit("关窗口"); return NO; }
- (void)windowDidResize:(NSNotification *)n { (void)n; layout_game(); }
- (void)windowDidChangeBackingProperties:(NSNotification *)n { (void)n; layout_game(); }
- (void)quit:(id)s { (void)s; app_quit("菜单退出"); }
- (void)keys:(id)s { (void)s; show_keys(); }
@end
static uint64_t g_btn;
static int g_w, g_a, g_s, g_d;

void *front_metal_layer(void) { return (__bridge void *)g_layer; }
// 原版 16:9 常数，行为不变；DM_GAME_RES=宽x高（主机模式，掌机按 720/900 同比）只供与 iOS 宽屏对拍
// The original game's 16:9 constants, behavior unchanged; DM_GAME_RES=<width>x<height> (docked mode; handheld scaled by 720/900) is only for side-by-side comparison with iOS widescreen
uint64_t front_game_res(int docked) {
    unsigned w, h; const char *e = getenv("DM_GAME_RES");
    if (e && sscanf(e, "%ux%u", &w, &h) == 2) return docked ? (uint64_t)h << 32 | w : (uint64_t)(h * 4 / 5) << 32 | (w * 4 / 5 + 3) / 4 * 4;
    return docked ? 900ull << 32 | 1600 : 720ull << 32 | 1280;
}

static int g_wheel;   // 滚轮：+1 / −1 = 右摇杆向上 / 向下推一小会儿（原版手柄版用右摇杆滚列表） | Wheel: +1 / -1 = push the right stick up / down for a short while (the original gamepad UI scrolls lists with the right stick)
static void push(void) {
    int32_t lx = (g_d - g_a) * 32767, ly = (g_w - g_s) * 32767;
    dm_input_set(g_btn, lx, ly, 0, g_wheel * 32767);
}
static int key_bit(unsigned short code) {
    switch (code) {
        case 38: return 0;  case 40: return 1;  case 32: return 2;  case 34: return 3;    // J K U I → A B X Y
        case 3: return 4;   case 5: return 5;                                            // F G → 按摇杆 | F G → press stick
        case 12: return 6;  case 14: return 7;  case 6: return 8;   case 8: return 9;    // Q E Z C → L R ZL ZR
        case 36: return 10; case 48: return 11;                                          // 回车 Tab → + − | Return Tab → + −
        case 123: return 12; case 126: return 13; case 124: return 14; case 125: return 15; // ← ↑ → ↓
    }
    return -1;
}
static void on_key(NSEvent *e, int down) {
    unsigned short c = e.keyCode;
    switch (c) { case 13: g_w = down; break; case 0: g_a = down; break; case 1: g_s = down; break; case 2: g_d = down; break; }
    int b = key_bit(c);
    if (b >= 0) g_btn = down ? (g_btn | 1ull << b) : (g_btn & ~(1ull << b));
    push();
}

typedef struct { void (*fn)(void); } guest_arg;
static void *guest_tramp(void *p) {
    static char alt[65536];                       // 信号备用栈是每线程的；崩溃打印要在客体线程上也生效 | The signal alternate stack is per thread; crash printing must also work on the guest thread
    stack_t ss = {.ss_sp = alt, .ss_size = sizeof alt}; sigaltstack(&ss, 0);
    ((guest_arg *)p)->fn(); LOG("客体线程结束"); exit(0);
}

void front_run(void (*guest)(void)) {
    if (!getenv("DM_WINDOW")) { guest(); return; }
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        NSRect r = NSMakeRect(0, 0, 1280, 720);
        NSWindow *win = [[NSWindow alloc] initWithContentRect:r
                                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskResizable
                                                      backing:NSBackingStoreBuffered defer:NO];
        win.title = @"D2R Mobile";
        NSView *v = win.contentView; g_view = v;
        v.wantsLayer = YES; v.layer.backgroundColor = CGColorGetConstantColor(kCGColorBlack);
        win.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary; win.contentMinSize = NSMakeSize(640, 360);
        static DmWinCtl *ctl; ctl = [DmWinCtl new]; win.delegate = ctl;
        g_layer = [CAMetalLayer layer];
        g_layer.device = MTLCreateSystemDefaultDevice();
        g_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        g_layer.contentsScale = win.backingScaleFactor;
        [v.layer addSublayer:g_layer]; layout_game();
        [win center];
        g_live = getenv("DM_APP") || getenv("DM_TOUCH_LIVE"); if (g_live) dm_touch_host_live_enable();
        { // 菜单栏：应用（退出 ⌘Q）/ 显示（全屏 ⌃⌘F）/ 帮助（键位对照） | Menu bar: application (Quit ⌘Q) / View (Full Screen ⌃⌘F) / Help (key mapping)
          NSMenu *bar = [NSMenu new];
          NSMenu *(^add)(const char *) = ^NSMenu *(const char *t) { NSMenuItem *it = [NSMenuItem new]; [bar addItem:it]; NSMenu *m = [[NSMenu alloc] initWithTitle:@(t)]; it.submenu = m; return m; };
          [[add("") addItemWithTitle:@(ui("dmAppQuit")) action:@selector(quit:) keyEquivalent:@"q"] setTarget:ctl];
          NSMenuItem *fs = [add(ui("dmAppMenuView")) addItemWithTitle:@(ui("dmAppFullscreen")) action:@selector(toggleFullScreen:) keyEquivalent:@"f"]; fs.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
          [[add(ui("dmAppMenuHelp")) addItemWithTitle:@(ui("dmAppKeysTitle")) action:@selector(keys:) keyEquivalent:@""] setTarget:ctl];
          NSApp.mainMenu = bar; }
        { void dm_gamepad_start(void (*on_mode)(int pad)); dm_gamepad_start(NULL); }   // 手柄（与 iOS 壳共用 ios/Sources/dm_gamepad.m）：连上即用，与键盘并存 | Gamepad (ios/Sources/dm_gamepad.m, shared with the iOS shell): works as soon as it is connected, alongside the keyboard
        [win makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
        [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskKeyDown | NSEventMaskKeyUp handler:^NSEvent *(NSEvent *e) {
            if (e.window != win) return e;   // 只吃游戏窗口的键；软键盘输入框等其他窗口照常收字 | Only swallow keys for the game window; other windows such as the software keyboard input box receive typing as usual
            if (e.modifierFlags & NSEventModifierFlagCommand) return e;   // 带 ⌘ 的留给菜单（⌘Q、⌃⌘F） | Keys with ⌘ are left to the menu (⌘Q, ⌃⌘F)
            if (!e.isARepeat) on_key(e, e.type == NSEventTypeKeyDown);
            return nil;   // 吃掉，免得系统提示音 | Swallow it to avoid the system beep
        }];
        // 鼠标当单点触屏（本机测触屏用，front.h dm_touch_set）：左键按下 / 拖动 = 手指 0 在归一化坐标处，松开 = 抬起
        // Mouse as a single-point touchscreen (for testing touch on the local machine, front.h dm_touch_set): left button down / drag = finger 0 at the normalized coordinates, release = lifted
        [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskLeftMouseDown | NSEventMaskLeftMouseDragged | NSEventMaskLeftMouseUp handler:^NSEvent *(NSEvent *e) {
            if (e.window != win) return e;
            if (e.type == NSEventTypeLeftMouseUp) { if (g_live) dm_touch_host_live(2, 0, 0); else dm_touch_set(NULL, 0); return nil; }
            NSPoint p = [v convertPoint:e.locationInWindow fromView:nil]; NSRect gr = game_rect();
            if (gr.size.width <= 0 || gr.size.height <= 0) return e;
            float nx = (float)((p.x - gr.origin.x) / gr.size.width), ny = (float)(1.0 - (p.y - gr.origin.y) / gr.size.height);   // AppKit 视图原点在左下，触屏原点在左上 | The AppKit view origin is at bottom left, the touchscreen origin is at top left
            if (e.type == NSEventTypeLeftMouseDown && (nx < 0 || nx > 1 || ny < 0 || ny > 1)) return nil;   // 按在黑边上：不算点 | Pressed on the black bars: not a click
            dm_touch t = {0, nx < 0 ? 0 : nx > 1 ? 1 : nx, ny < 0 ? 0 : ny > 1 ? 1 : ny};
            if (g_live) { dm_touch_host_live(e.type == NSEventTypeLeftMouseDown ? 0 : 1, t.x, t.y); return nil; }
            dm_touch_set(&t, 1); return nil;
        }];
        // 右键 = 手柄 B（返回 / 关面板）；滚轮 = 右摇杆上下推 0.12 秒（滚列表）。只在 App 形态接（命令行宿主的测试不受影响）
        // Right click = gamepad B (back / close panel); wheel = push the right stick up or down for 0.12 s (scroll lists). Only wired in App form (tests of the command-line host are unaffected)
        if (g_live) {
            [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskRightMouseDown | NSEventMaskRightMouseUp handler:^NSEvent *(NSEvent *e) {
                if (e.window != win) return e;
                if (e.type == NSEventTypeRightMouseDown) g_btn |= DM_BTN_B; else g_btn &= ~(uint64_t)DM_BTN_B;
                push(); return nil;
            }];
            [NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel handler:^NSEvent *(NSEvent *e) {
                if (e.window != win || e.scrollingDeltaY == 0) return e;
                static int gen; int my = ++gen; g_wheel = e.scrollingDeltaY > 0 ? 1 : -1; push();
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.12 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ if (my == gen) { g_wheel = 0; push(); } });
                return nil;
            }];
        }
        if (getenv("DM_APP") && !getenv("DM_APP_ROMFS") && ![NSUserDefaults.standardUserDefaults boolForKey:@"KeysShown"]) { show_keys(); [NSUserDefaults.standardUserDefaults setBool:YES forKey:@"KeysShown"]; }   // 第一次启动给一次键位对照（界面上的按键提示是手柄字母）；以后在「帮助」菜单里 | Show the key mapping once on first launch (button prompts on screen are gamepad letters); afterwards it is in the Help menu
        { const char *e;   // 测试（没人点窗口时用）：DM_TEST_WINSIZE=宽x高 设内容区大小；DM_TEST_FULLSCREEN_AT=<秒> 进全屏；DM_TEST_QUIT_AT=<秒> 走一次退出 | Tests (for when nobody is there to click the window): DM_TEST_WINSIZE=WxH sets the content size; DM_TEST_FULLSCREEN_AT=<seconds> enters full screen; DM_TEST_QUIT_AT=<seconds> goes through one quit
          int w, h; if ((e = getenv("DM_TEST_WINSIZE")) && sscanf(e, "%dx%d", &w, &h) == 2) { [win setContentSize:NSMakeSize(w, h)]; layout_game(); }
          if ((e = getenv("DM_TEST_FULLSCREEN_AT"))) dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(atof(e) * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ [win toggleFullScreen:nil]; });
          if ((e = getenv("DM_TEST_QUIT_AT"))) dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(atof(e) * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{ app_quit("DM_TEST_QUIT_AT"); }); }
        // 生命周期（hle.h / dm.h）：失焦 → 2、拿回焦点 → 1；最小化 → 3 + 音频暂停 + GPU 挂起，还原 → 恢复（随后拿回焦点 → 1）
        // Lifecycle (hle.h / dm.h): focus lost → 2, focus regained → 1; minimized → 3 + audio paused + GPU suspended, restored → resume (followed by focus regained → 1)
        NSNotificationCenter *nc = NSNotificationCenter.defaultCenter;
        [nc addObserverForName:NSWindowDidResignKeyNotification object:win queue:nil usingBlock:^(NSNotification *n) { (void)n; if (!win.miniaturized) dm_focus_set(2); g_btn = 0; g_w = g_a = g_s = g_d = 0; push(); { void dm_gamepad_reset(void); dm_gamepad_reset(); } }];   // 失焦：键盘和手柄按着的键清掉（松开事件收不到） | Focus lost: clear the keys held on keyboard and gamepad (their release events will not arrive)
        [nc addObserverForName:NSWindowDidBecomeKeyNotification object:win queue:nil usingBlock:^(NSNotification *n) { (void)n; dm_focus_set(1); }];
        [nc addObserverForName:NSWindowDidMiniaturizeNotification object:win queue:nil usingBlock:^(NSNotification *n) { (void)n; dm_focus_set(3); dm_audio_suspend(1); dm_gfx_suspend(1); }];
        [nc addObserverForName:NSWindowDidDeminiaturizeNotification object:win queue:nil usingBlock:^(NSNotification *n) { (void)n; dm_gfx_suspend(0); dm_audio_suspend(0); dm_focus_set(2); }];
        // 客体线程：栈 64 MiB（Switch 主线程栈由 npdm 指定，宿主给足）
        // Guest thread: 64 MiB stack (the Switch main thread stack is specified by npdm; the host gives plenty)
        static guest_arg ga; ga.fn = guest;
        pthread_attr_t a; pthread_attr_init(&a); pthread_attr_setstacksize(&a, 64u << 20);
        pthread_t th; if (pthread_create(&th, &a, guest_tramp, &ga)) die("pthread_create guest");
        [NSApp run];
    }
}

// 软键盘输入框（front.h）：主线程弹 NSAlert + 文本框，客体线程同步等结果。无窗口模式不弹（返回 -1，调用方按取消）
// Software keyboard input box (front.h): the main thread shows an NSAlert + text field and the guest thread waits synchronously for the result. Not shown in windowless mode (returns -1, the caller treats it as cancel)
int front_text_input(const char *title, const char *sub, const char *guide, const char *initial, int max_chars, char *out, int out_cap) {
    if (!g_layer) return -1;
    __block int r = 0;
    __block NSString *text = nil;
    dispatch_sync(dispatch_get_main_queue(), ^{
        NSAlert *a = [NSAlert new];
        a.messageText = title && *title ? @(title) : @"输入";
        NSString *info = [NSString stringWithFormat:@"%s%s%s", sub ? sub : "", sub && *sub && guide && *guide ? "\n" : "", guide ? guide : ""];
        a.informativeText = info;
        [a addButtonWithTitle:@"确定"]; [a addButtonWithTitle:@"取消"];
        NSTextField *f = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 300, 24)];
        f.stringValue = initial ? @(initial) : @"";
        if (guide && *guide) f.placeholderString = @(guide);
        a.accessoryView = f; a.window.initialFirstResponder = f;
        r = [a runModal] == NSAlertFirstButtonReturn;
        text = f.stringValue;
    });
    if (!r) return 0;
    (void)max_chars;                              // 截断按 UTF-16 单元在 hle_swkbd.c 统一做 | Truncation is done uniformly in hle_swkbd.c in UTF-16 units
    snprintf(out, out_cap, "%s", text.UTF8String);
    return 1;
}
