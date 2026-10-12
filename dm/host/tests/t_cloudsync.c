// iCloud 同步核心单测（秒级）：dm_cloudsync.c。两个本地目录当两台设备、一个本地目录当云端；期望都是按「后写覆盖先写」手推的，不跑被测实现得出。
// iCloud sync core unit test (takes seconds): dm_cloudsync.c. Two local directories act as two devices and one local directory as the cloud; all expectations are derived by hand from "later write overwrites earlier write", not obtained by running the implementation under test.
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
int dm_cloud_sync(const char *save, const char *cloud, const char *bak, const char *statef, int allow_down, uint64_t now_ms, int (*ready)(const char *unit));
int dm_cloud_conflict_take(void);
#if __has_include("../stash_seed.h")   // 公开仓库不带这份预置（它是游戏生成的一份存档的字节）：不在时跳过用到它的 ⑱ ⑲ 两组 | the public repository does not ship this seed (it is the bytes of a save the game generated): when it is absent, the two groups that use it, 18 and 19, are skipped
#include "../stash_seed.h"
#define T_HAVE_STASH_SEED 1
#endif

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)
static char R[512];
static void path(char *o, const char *a, const char *b) { snprintf(o, 1024, "%s/%s/%s", R, a, b); }
static void put(const char *dev, const char *rel, const char *txt, long sec) {   // 写一个文件并把修改时间定成 sec 秒 | write a file and set its modification time to sec seconds
    char p[1024]; path(p, dev, rel); char d[1024]; strcpy(d, p); for (char *s = d + strlen(R) + 1; (s = strchr(s, '/')); s++) { *s = 0; mkdir(d, 0755); *s = '/'; }
    FILE *f = fopen(p, "w"); fputs(txt, f); fclose(f); struct timeval tv[2] = { { sec, 0 }, { sec, 0 } }; utimes(p, tv);
}
static const char *get(const char *dev, const char *rel) { static char b[256]; char p[1024]; path(p, dev, rel); FILE *f = fopen(p, "r"); if (!f) return NULL; size_t n = fread(b, 1, 255, f); b[n] = 0; fclose(f); return b; }
static long mtime(const char *dev, const char *rel) { char p[1024]; struct stat st; path(p, dev, rel); return stat(p, &st) ? -1 : st.st_mtimespec.tv_sec; }
static int dsync(const char *dev, int down, uint64_t now) { char s[1024], c[1024], b[1024], t[1024]; path(s, dev, "save"); snprintf(c, sizeof c, "%s/cloud", R); path(b, dev, "bak"); path(t, dev, "state"); return dm_cloud_sync(s, c, b, t, down, now, NULL); }
static int count_bak(const char *dev, const char *pre) { char d[1024]; path(d, dev, "bak"); DIR *dp = opendir(d); struct dirent *e; int n = 0; while (dp && (e = readdir(dp))) n += !strncmp(e->d_name, pre, strlen(pre)); if (dp) closedir(dp); return n; }
static int no_skip(const char *u) { return strcmp(u, "Alice"); }
// 共享仓库档：一页物品（items 件，只写页头与件数）+ 一段编年史（chron 字节）；侧文件：n 件。只造结构，同步那边只看结构（dm_cloudsync.c rich_of）
// Shared stash file: one page of items (items pieces, only the page header and item count are written) + one chronicle section (chron bytes); side file: n pieces. Only the structure is built, as the sync side only looks at structure (dm_cloudsync.c rich_of)
static void put_bin(const char *dev, const char *rel, const uint8_t *d, size_t n, long sec) {
    char p[1024]; path(p, dev, rel); char dd[1024]; strcpy(dd, p); for (char *x = dd + strlen(R) + 1; (x = strchr(x, '/')); x++) { *x = 0; mkdir(dd, 0755); *x = '/'; }
    FILE *f = fopen(p, "wb"); fwrite(d, 1, n, f); fclose(f); struct timeval tv[2] = { { sec, 0 }, { sec, 0 } }; utimes(p, tv);
}
static void put_d2i(const char *dev, int items, int chron, long sec) {
    static uint8_t b[8192]; memset(b, 0, sizeof b); uint32_t l1 = 68, l2 = (uint32_t)chron;
    memcpy(b, "\x55\xaa\x55\xaa", 4); memcpy(b + 16, &l1, 4); memcpy(b + 64, "JM", 2); b[66] = (uint8_t)items;
    memcpy(b + 68, "\x55\xaa\x55\xaa", 4); memcpy(b + 68 + 16, &l2, 4); b[68 + 64] = 0xc0;
    put_bin(dev, "save/ModernSharedStashSoftCoreV2/ModernSharedStashSoftCoreV2.d2i", b, 68 + l2, sec);
}
static void put_vault(const char *dev, int n, long sec) { uint8_t b[8] = { 'S', 'W', 'V', 'T', 1, 0, (uint8_t)n, 0 }; put_bin(dev, "save/ModernSharedStashSoftCoreV2.swvault", b, 8, sec); }
static int d2i_items(const char *dev, const char *rel) { char p[1024]; path(p, dev, rel); FILE *f = fopen(p, "rb"); if (!f) return -1; uint8_t b[70]; size_t n = fread(b, 1, 70, f); fclose(f); return n >= 67 ? b[66] : -1; }

int main(void) {
    snprintf(R, sizeof R, "%s/t_cloudsync.%d", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid()); mkdir(R, 0755);
    char c[1024]; snprintf(c, sizeof c, "%s/cloud", R); mkdir(c, 0755);
    // ① A 有角色 Alice（时刻 1000 秒）、共享仓库、Settings；B 空 → A 上传，B 启动拿到 Alice 与共享仓库，Settings 不同步，修改时间照 A 的
    // (1) A has character Alice (timestamp 1000 seconds), the shared stash and Settings; B is empty -> A uploads, B gets Alice and the shared stash at startup, Settings is not synced, and modification times follow A's
    put("A", "save/Alice/Alice.d2s", "a1", 1000); put("A", "save/Alice/Alice.key", "k", 900);
    put("A", "save/SharedStashSoftCoreV2.d2i", "s1", 1100); put("A", "save/ModernSharedStashSoftCoreV2/ModernSharedStashSoftCoreV2.d2i", "m1", 1100); put("A", "save/Settings/Settings.json", "{}", 1100);
    put("B", "save/Settings/Settings.json", "{b}", 1200);
    CHECK(dsync("A", 0, 0) == 2, "① A 上传应为 2 个单位（Alice、_stash）");
    CHECK(dsync("B", 1, 0) == 2, "① B 下载应为 2 个单位");
    CHECK(get("B", "save/Alice/Alice.d2s") && !strcmp(get("B", "save/Alice/Alice.d2s"), "a1"), "① B 的 Alice 应是 a1");
    CHECK(mtime("B", "save/Alice/Alice.d2s") == 1000, "① 修改时间应还原成 1000，实为 %ld", mtime("B", "save/Alice/Alice.d2s"));
    CHECK(get("B", "save/ModernSharedStashSoftCoreV2/ModernSharedStashSoftCoreV2.d2i") && !strcmp(get("B", "save/ModernSharedStashSoftCoreV2/ModernSharedStashSoftCoreV2.d2i"), "m1"), "① B 的共享仓库应是 m1");
    CHECK(!strcmp(get("B", "save/Settings/Settings.json"), "{b}"), "① B 的 Settings 不该被动");
    CHECK(dsync("A", 1, 0) == 0 && dsync("B", 1, 0) == 0, "① 再同步一次两边都应无动作");
    // ② 只有 B 改（2000）→ B 运行中上传；A 启动拿到，A 原来那份进备份
    // (2) Only B changed (2000) -> B uploads while running; A gets it at startup, and A's former copy goes to backup
    put("B", "save/Alice/Alice.d2s", "b2", 2000);
    CHECK(dsync("B", 0, 0) == 1, "② B 应上传 1 个");
    CHECK(dsync("A", 0, 0) == 0 && !strcmp(get("A", "save/Alice/Alice.d2s"), "a1"), "② A 运行中不该改本地");
    CHECK(dsync("A", 1, 0) == 1 && !strcmp(get("A", "save/Alice/Alice.d2s"), "b2"), "② A 启动后应是 b2");
    CHECK(get("A", "bak/Alice-1000000/Alice/Alice.d2s") && !strcmp(get("A", "bak/Alice-1000000/Alice/Alice.d2s"), "a1"), "② A 被覆盖的 a1 应在备份 Alice-1000000");
    // ③ 两边都改，B 后写（A 3000 先传，B 3500）→ B 运行中照样上传（后写赢），云端原来的 a3 进 B 的备份；A 启动拿到 b3
    // (3) Both sides changed, B wrote later (A's 3000 uploaded first, B 3500) -> B still uploads while running (last write wins), the cloud's former a3 goes to B's backup; A gets b3 at startup
    put("A", "save/Alice/Alice.d2s", "a3", 3000); put("B", "save/Alice/Alice.d2s", "b3", 3500);
    CHECK(dsync("A", 0, 0) == 1, "③ A 先上传");
    CHECK(dsync("B", 0, 0) == 1, "③ B 后写也上传");
    CHECK(get("B", "bak/Alice-3000000/Alice/Alice.d2s") && !strcmp(get("B", "bak/Alice-3000000/Alice/Alice.d2s"), "a3"), "③ 被覆盖的云端 a3 应在 B 的备份");
    CHECK(dsync("A", 1, 0) == 1 && !strcmp(get("A", "save/Alice/Alice.d2s"), "b3"), "③ A 启动后应是 b3");
    // ④ 两边都改，A 后写（A 5000 已传；B 4000 没传就重启）→ B 启动拿 A 的 a4，b4 进备份 Alice-4000000
    // (4) Both sides changed, A wrote later (A's 5000 uploaded; B's 4000 not uploaded before restarting) -> B gets A's a4 at startup, b4 goes to backup Alice-4000000
    put("A", "save/Alice/Alice.d2s", "a4", 5000); CHECK(dsync("A", 0, 0) == 1, "④ A 上传");
    put("B", "save/Alice/Alice.d2s", "b4", 4000);
    CHECK(dsync("B", 1, 0) == 1 && !strcmp(get("B", "save/Alice/Alice.d2s"), "a4"), "④ B 启动后应是 a4（云端 5000 > 本机 4000）");
    CHECK(get("B", "bak/Alice-4000000/Alice/Alice.d2s") && !strcmp(get("B", "bak/Alice-4000000/Alice/Alice.d2s"), "b4"), "④ B 被覆盖的 b4 应在备份");
    // ⑤ 云端的包还没下载好（ready 返回 0）→ 跳过，本地不动
    // (5) The cloud package has not finished downloading (ready returns 0) -> skip, local files untouched
    put("A", "save/Alice/Alice.d2s", "a5", 6000); CHECK(dsync("A", 0, 0) == 1, "⑤ A 上传");
    { char s[1024], b[1024], t[1024]; path(s, "B", "save"); path(b, "B", "bak"); path(t, "B", "state"); CHECK(dm_cloud_sync(s, c, b, t, 1, 0, no_skip) == 0 && !strcmp(get("B", "save/Alice/Alice.d2s"), "a4"), "⑤ 没下载好时 B 应不动"); }
    CHECK(dsync("B", 1, 0) == 1 && !strcmp(get("B", "save/Alice/Alice.d2s"), "a5"), "⑤ 下载好后 B 应是 a5");
    // ⑥ A 删 Alice → 云端删除标记；B 启动也删，B 那份进备份；B 的备份只留 3 份（此前有 1000000 不在 B、3000000、4000000，再加 6000000 = 3 份）
    // (6) A deletes Alice -> deletion marker in the cloud; B also deletes at startup and B's copy goes to backup; B's backup keeps only 3 copies (before this there were 1000000 not on B, 3000000, 4000000, plus 6000000 = 3 copies)
    { char p[1024]; path(p, "A", "save/Alice/Alice.d2s"); unlink(p); path(p, "A", "save/Alice/Alice.key"); unlink(p); path(p, "A", "save/Alice"); rmdir(p); }
    CHECK(dsync("A", 0, 7000000) == 1, "⑥ A 应写删除标记");
    CHECK(dsync("B", 1, 0) == 1 && !get("B", "save/Alice/Alice.d2s"), "⑥ B 启动后 Alice 应被删");
    CHECK(get("B", "bak/Alice-6000000/Alice/Alice.d2s") && !strcmp(get("B", "bak/Alice-6000000/Alice/Alice.d2s"), "a5"), "⑥ B 被删的 a5 应在备份");
    CHECK(count_bak("B", "Alice-") == 3, "⑥ B 的 Alice 备份应留 3 份，实为 %d", count_bak("B", "Alice-"));
    for (long t = 8; t <= 9; t++) { char v[8]; snprintf(v, sizeof v, "x%ld", t); put("B", "save/Bob/Bob.d2s", v, t * 1000); }   // 无关角色不受影响 | unrelated characters are not affected
    CHECK(dsync("B", 1, 0) == 1, "⑥ B 的新角色 Bob 上传");
    // ⑦ 删除之后 A 又建了同名角色（9500）→ 上传（复活）；B 启动拿到
    // (7) After the deletion A created a character with the same name again (9500) -> upload (revive); B gets it at startup
    put("A", "save/Alice/Alice.d2s", "a7", 9500);
    CHECK(dsync("A", 1, 0) == 2 && !strcmp(get("A", "save/Bob/Bob.d2s"), "x9"), "⑦ A：Alice 上传 + Bob 下载");
    CHECK(dsync("B", 1, 0) == 1 && get("B", "save/Alice/Alice.d2s") && !strcmp(get("B", "save/Alice/Alice.d2s"), "a7"), "⑦ B 启动后应有新建的 Alice a7");
    // ⑧ 共享仓库：B 改（10000）→ A 启动拿到
    // (8) Shared stash: B changes it (10000) -> A gets it at startup
    put("B", "save/SharedStashSoftCoreV2.d2i", "s8", 10000);
    CHECK(dsync("B", 0, 0) == 1 && dsync("A", 1, 0) == 1 && !strcmp(get("A", "save/SharedStashSoftCoreV2.d2i"), "s8"), "⑧ A 的共享仓库应是 s8");
    CHECK(!strcmp(get("A", "save/ModernSharedStashSoftCoreV2/ModernSharedStashSoftCoreV2.d2i"), "m1"), "⑧ 共享仓库的另一个文件应保留 m1");
    // ⑨ 存档目录被游戏以外的原因清空（状态文件还在）→ 启动时不算删除，从云端全拿回来，云端不出现删除标记
    // (9) The save directory was emptied by something other than the game (the state file is still there) -> at startup it does not count as a deletion, everything is fetched back from the cloud, and no deletion marker appears in the cloud
    { char p[1024]; path(p, "A", "save"); char cmd[1200]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", p); system(cmd); mkdir(p, 0755); }
    CHECK(dsync("A", 1, 0) == 3, "⑨ A 启动应下载 3 个（Alice、Bob、_stash）");
    CHECK(get("A", "save/Alice/Alice.d2s") && !strcmp(get("A", "save/Alice/Alice.d2s"), "a7") && get("A", "save/Bob/Bob.d2s") && !strcmp(get("A", "save/SharedStashSoftCoreV2.d2i"), "s8"), "⑨ A 应拿回 Alice a7、Bob、共享仓库 s8");
    CHECK(dsync("B", 1, 0) == 0 && get("B", "save/Alice/Alice.d2s"), "⑨ B 不受影响（没有删除标记传过来）");
    // ⑩ 备份目录写不进（这里用同名普通文件把它堵住）→ 覆盖和删除都不动本地；解开后照常进行（原先备份写失败仍照样覆盖 / 删）
    // (10) The backup directory cannot be written (here it is blocked with a regular file of the same name) -> neither overwrite nor delete touches local files; after unblocking it proceeds as usual (previously it still overwrote / deleted when the backup write failed)
    { char s[1024], c2[1024], b[1024], t2[1024], bf[1024]; snprintf(c2, sizeof c2, "%s/cloud2", R); mkdir(c2, 0755);
      #define SY(dev, down, now) (path(s, dev, "save"), path(b, dev, "bak"), path(t2, dev, "state"), dm_cloud_sync(s, c2, b, t2, down, now, NULL))
      put("F", "save/Settings/Settings.json", "{}", 1);   // F 是一台已有存档目录的设备 | F is a device that already has a save directory
      put("E", "save/Eve/Eve.d2s", "e1", 1000); CHECK(SY("E", 0, 0) == 1 && SY("F", 1, 0) == 1 && get("F", "save/Eve/Eve.d2s") && !strcmp(get("F", "save/Eve/Eve.d2s"), "e1"), "⑩ 准备：F 拿到 Eve e1");
      put("E", "save/Eve/Eve.d2s", "e2", 2000); CHECK(SY("E", 0, 0) == 1, "⑩ E 上传 e2");
      path(bf, "F", "bak"); FILE *f = fopen(bf, "w"); fclose(f);          // F 的备份目录位置是个普通文件：建不了目录 | F's backup directory location is a regular file: the directory cannot be created
      CHECK(SY("F", 1, 0) == 0 && get("F", "save/Eve/Eve.d2s") && !strcmp(get("F", "save/Eve/Eve.d2s"), "e1"), "⑩ 备份写不进时 F 不应覆盖，仍是 e1");
      unlink(bf);
      CHECK(SY("F", 1, 0) == 1 && get("F", "save/Eve/Eve.d2s") && !strcmp(get("F", "save/Eve/Eve.d2s"), "e2") && get("F", "bak/Eve-1000000/Eve/Eve.d2s") && !strcmp(get("F", "bak/Eve-1000000/Eve/Eve.d2s"), "e1"), "⑩ 解开后 F 换成 e2，e1 整份在备份里");
      { char p2[1024]; path(p2, "E", "save/Eve/Eve.d2s"); unlink(p2); path(p2, "E", "save/Eve"); rmdir(p2); }
      CHECK(SY("E", 0, 3000000) == 1, "⑩ E 运行中删了 Eve，写删除标记");
      char bk[1100]; snprintf(bk, sizeof bk, "%s.keep", bf); rename(bf, bk); f = fopen(bf, "w"); fclose(f);   // 已有的备份目录先挪开，再用普通文件堵住 | move the existing backup directory aside first, then block it with a regular file
      CHECK(SY("F", 1, 0) == 0 && get("F", "save/Eve/Eve.d2s"), "⑩ 备份写不进时 F 不应删 Eve");
      unlink(bf); rename(bk, bf);
      CHECK(SY("F", 1, 0) == 1 && !get("F", "save/Eve/Eve.d2s") && get("F", "bak/Eve-2000000/Eve/Eve.d2s"), "⑩ 解开后 F 删 Eve，e2 整份在备份里");
      #undef SY
    }
    // ⑪–⑮ 编年史被整份换空：新设备首次同步以云端为准；共享仓库不许被「明显更少」的一份覆盖（另一个云端目录 cloud3，设备 G / H）
    // (11)-(15) Chronicle replaced wholesale by an empty one: a new device's first sync takes the cloud as authoritative; the shared stash must not be overwritten by a copy with "clearly less" (another cloud directory cloud3, devices G / H)
    { char s3[1024], c3[1024], b3[1024], t3[1024]; snprintf(c3, sizeof c3, "%s/cloud3", R); mkdir(c3, 0755);
      #define S3(dev, down) (path(s3, dev, "save"), path(b3, dev, "bak"), path(t3, dev, "state"), dm_cloud_sync(s3, c3, b3, t3, down, 0, NULL))
      #define D2I "save/ModernSharedStashSoftCoreV2/ModernSharedStashSoftCoreV2.d2i"
      put_d2i("G", 5, 300, 1000); put_vault("G", 3, 1000);
      CHECK(S3("G", 0) == 1, "⑫ 云端为空：G 首次同步上传共享仓库");
      dm_cloud_conflict_take();
      put_d2i("H", 0, 148, 5000);   // H 新装：游戏刚建的空共享仓库，时刻比云端新 | H freshly installed: the empty shared stash the game just created, with a timestamp newer than the cloud's
      CHECK(S3("H", 0) == 0 && d2i_items("H", D2I) == 0, "⑪ 新设备运行中首次同步：不传不改（不能拿空仓库盖云端）");
      CHECK(S3("H", 1) == 1 && d2i_items("H", D2I) == 5 && get("H", "save/ModernSharedStashSoftCoreV2.swvault"), "⑪ 新设备启动首次同步：以云端为准，拿到 5 件与编年史仓库");
      CHECK(count_bak("H", "_stash-") == 1, "⑪ 新设备本来那份空仓库进备份");
      CHECK(!dm_cloud_conflict_take(), "⑪ 正常首次同步不算冲突");
      put_d2i("G", 6, 400, 6000); CHECK(S3("G", 0) == 1, "⑭ G 编年史变长（400）照常上传");
      CHECK(S3("H", 1) == 1 && d2i_items("H", D2I) == 6, "⑭ H 启动拿到 G 的新版");
      put_d2i("H", 0, 148, 7000);   // H 上的共享仓库不知为何变空（编年史 148、0 件），时刻更新 | the shared stash on H somehow became empty (chronicle 148, 0 items), with a newer timestamp
      CHECK(S3("H", 0) == 0, "⑬ H 运行中要上传变空的那份：拦下");
      CHECK(dm_cloud_conflict_take() == 1, "⑬ 拦下记冲突（进局弹提示）");
      CHECK(S3("G", 1) == 0 && d2i_items("G", D2I) == 6, "⑬ 云端没被盖：G 仍是 6 件");
      put_d2i("H", 7, 500, 7500); CHECK(S3("H", 0) == 1, "⑮ H 正常玩：编年史 500 上传");
      put_d2i("G", 6, 450, 7600);   // G 没同步到 H 的 500 就自己写了 450（两边都改，G 后写） | G wrote 450 itself without having synced H's 500 (both sides changed, G wrote later)
      CHECK(S3("G", 0) == 0, "⑮ G 运行中要拿编年史 450 盖云端的 500：拦下");
      CHECK(S3("G", 1) == 0 && d2i_items("G", D2I) == 6, "⑮ G 启动：云端 500 比本机 450 时刻早但编年史长——按后写赢会上传，被拦；本机不动");
      CHECK(dm_cloud_conflict_take() == 1, "⑮ 记冲突");
      { char p3[1024]; path(p3, "H", "save/ModernSharedStashSoftCoreV2.swvault"); unlink(p3); } put_d2i("H", 7, 500, 8000);
      CHECK(S3("H", 0) == 0 && dm_cloud_conflict_take() == 1, "⑬ 编年史仓库侧文件没了（云端有 3 件）：拦下");
      put_vault("H", 0, 8100); put_d2i("H", 0, 500, 8100);
      CHECK(S3("H", 0) == 1 && !dm_cloud_conflict_take(), "⑯ 正常玩法：共享仓库物品取空、编年史仓库取空（文件还在，0 件），编年史没变短——照常上传");
      // ⑰ rf3 那个形状：云端专家仓库 2 件、编年史还是新建的 148；本机这份被换成游戏新建的空档（0 件、148），状态记录还在、云端没变 = 走「只本机改了」的上传
      // (17) That rf3 shape: cloud hardcore stash has 2 items and the chronicle is still the freshly created 148; the local copy was replaced by the empty file the game freshly creates (0 items, 148), the state record is still there and the cloud is unchanged = takes the "only local changed" upload
      { char cd[1024]; snprintf(cd, sizeof cd, "%s/cloud4", R); mkdir(cd, 0755);
        #define S4(dev, down) (path(s3, dev, "save"), path(b3, dev, "bak"), path(t3, dev, "state"), dm_cloud_sync(s3, cd, b3, t3, down, 0, NULL))
        put_d2i("J", 2, 148, 1000); CHECK(S4("J", 1) == 1, "⑰ 准备：J 上传 2 件、编年史 148 的仓库");
        put_d2i("J", 0, 148, 2000); dm_cloud_conflict_take();
        CHECK(S4("J", 0) == 0 && dm_cloud_conflict_take() == 1, "⑰ 运行中要拿新建空档（0 件、148）盖 2 件的那份：拦下");
        CHECK(S4("J", 1) == 0, "⑰ 启动时同样拦下（不上传）");
        put_d2i("J", 0, 300, 3000); CHECK(S4("J", 0) == 1, "⑰ 编年史长过新建大小的仓库取空：照常上传");
        // ⑱ rf3 真形状：云端专家仓库是另一台的预置（stash_seed.h），本机还没有专家仓库档（启动同步跑在写预置之前）→ 不拦、不弹；本机写了预置后上传也不拦
        // (18) The real rf3 shape: the cloud hardcore stash is another device's preset (stash_seed.h), and the local device has no hardcore stash file yet (startup sync runs before the preset is written) -> not blocked, no prompt; uploading after the local device writes the preset is not blocked either
#ifdef T_HAVE_STASH_SEED
        { char cd5[1024]; snprintf(cd5, sizeof cd5, "%s/cloud5", R); mkdir(cd5, 0755);
          #define S5(dev, down) (path(s3, dev, "save"), path(b3, dev, "bak"), path(t3, dev, "state"), dm_cloud_sync(s3, cd5, b3, t3, down, 0, NULL))
          #define HC "save/ModernSharedStashHardCoreV2/ModernSharedStashHardCoreV2.d2i"
          put_bin("K", HC, g_stash_seed, sizeof g_stash_seed, 1000); put_d2i("K", 3, 400, 1000); CHECK(S5("K", 1) == 1, "⑱ 准备：K 上传（专家仓库是预置）");
          { char p5[1024]; path(p5, "K", HC); unlink(p5); } put_d2i("K", 3, 400, 2000); dm_cloud_conflict_take();
          S5("K", 0); CHECK(!dm_cloud_conflict_take(), "⑱ 本机还没有专家仓库档、云端只是预置：不算丢东西，不弹");
          put_bin("K", HC, g_stash_seed, sizeof g_stash_seed, 3000); S5("K", 0); CHECK(!dm_cloud_conflict_take(), "⑱ 本机写了预置再上传：不拦");
          // ⑲ 反过来：云端有真东西，本机是预置 → 照拦
          // (19) The reverse: the cloud has real content and the local copy is the preset -> still blocked
          put_d2i("K", 5, 600, 4000); S5("K", 0); dm_cloud_conflict_take();
          { uint8_t big[700]; memset(big, 0, sizeof big); uint32_t l1 = 68, l2 = 300; memcpy(big, "\x55\xaa\x55\xaa", 4); memcpy(big + 16, &l1, 4); memcpy(big + 64, "JM", 2); big[66] = 9; memcpy(big + 68, "\x55\xaa\x55\xaa", 4); memcpy(big + 84, &l2, 4); big[132] = 0xc0;
            put_bin("K", HC, big, 68 + 300, 5000); CHECK(S5("K", 0) == 1, "⑲ 准备：专家仓库有 9 件、编年史 300 上传"); }
          put_bin("K", HC, g_stash_seed, sizeof g_stash_seed, 6000);
          CHECK(S5("K", 0) == 0 && dm_cloud_conflict_take() == 1, "⑲ 本机变回预置要盖 9 件的那份：拦下");
          #undef HC
          #undef S5
        }
#else
        printf("跳过 ⑱ ⑲：没有共享仓库预置 stash_seed.h（公开仓库不带）\n");
#endif
        #undef S4
      }
      #undef D2I
      #undef S3
    }
    printf("t_cloudsync: %d 项，%d 失败\n", checks, fails);
    return fails != 0;
}
