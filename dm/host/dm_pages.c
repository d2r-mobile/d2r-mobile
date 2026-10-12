// 个人仓库 2–5 页。
// 角色存档里的个人仓库格（玩家物品栏页 4）放「当前页」；另外 4 页的物品位流放侧文件 <角色>/<角色>.swpage（存档工作目录里的角色目录，随存档提交原子换入；
// iCloud 同步按角色整个目录打包，侧文件跟着走，dm_cloudsync.c）。翻页在服务端拍里做（diag.c hud_tick 调 dm_pages_tick），全用原版函数：
//   搬出 = 写位流 main+0x581b60 → 从物品栏摘下 0x55e430 → 通知客户端 0x642f70(…, 0x20, 4, 0, 页, y, x) → 释放 0x658be0；
//   放回 = 读位流建物品 0x5793a0 + 0x6564e0（孔里的照原版复制物品 0x65a840 逐个建、0x64d080 镶回）→ 原版读档放物品 0x6f1940（按物品自带的页 / 格放进属主物品栏）
//          → 通知客户端 0x642f70(…, 1, 2, 0, 页, 0, x | y << 16)（同收包 0x18 的处理 0x6d0050）。
// 界面在 dm_cursor.c（个人页左右箭头、页码）。实测与坑：设计文档「个人仓库 2–5 页：实现与实测」。
// Personal stash pages 2–5.
// The personal stash grid in the character save (player item container page 4) holds the "current page"; the item bitstreams of the other 4 pages go in the sidecar file <character>/<character>.swpage (in the character directory inside the save working directory, swapped in atomically with the save commit;
// iCloud sync packs the whole directory per character, so the sidecar file goes along, dm_cloudsync.c). Page flips are done in the server tick (hud_tick in diag.c calls dm_pages_tick), using only original game functions:
//   move out = write bitstream main+0x581b60 -> remove from the item container 0x55e430 -> notify the client 0x642f70(..., 0x20, 4, 0, page, y, x) -> free 0x658be0;
//   put back = read bitstream and build item 0x5793a0 + 0x6564e0 (socketed ones are built one by one following the original game's item copy 0x65a840 and socketed back with 0x64d080) -> the original game's load-time item placement 0x6f1940 (puts it into the owner's item container by the item's own page / cell)
//          -> notify the client 0x642f70(..., 1, 2, 0, page, 0, x | y << 16) (same as the handler 0x6d0050 for received packet 0x18).
// UI is in dm_cursor.c (left / right arrows on the personal page, page number). Measurements and pitfalls: design doc 「个人仓库 2–5 页：实现与实测」(Personal stash pages 2–5: implementation and measurements).
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <dirent.h>
#include "dm.h"

#define VBYTES 1024   // 单件位流上限（含孔里的） | Upper limit of a single item's bitstream (including socketed ones)
const char *dm_save_workdir(void);   // hle_fs.c：事务式存档的工作目录 | hle_fs.c: the working directory of the transactional save
int dm_request_save(void);           // diag.c：请游戏走一次它自己的定时存盘 | diag.c: ask the game to run one of its own timed saves
static pthread_mutex_t g_pmu = PTHREAD_MUTEX_INITIALIZER;   // 服务端拍（翻页）与写盘线程（提交前钩子）都碰页数据 | Both the server tick (page flip) and the disk-write thread (pre-commit hook) touch the page data

int dm_item_write(uint8_t *b, uint64_t it, uint8_t *buf) {   // 原版写位流（模板 main+0x65a840）：写入器 {缓冲, 容量位, 字节, 余位, 错误}，上下文 = GOT 虚表 + 0x10 | The original game's bitstream write (template main+0x65a840): writer {buffer, capacity in bits, byte, remaining bits, error}, context = GOT vtable + 0x10
    struct { uint64_t buf, cap, n, bits; uint32_t err, pad; uint8_t rest[0x60]; } w; uint64_t ctx[2] = { *(uint64_t *)(b + 0x211feb8) + 0x10, 0 };
    memset(&w, 0, sizeof w); memset(buf, 0, VBYTES); w.buf = (uint64_t)buf; w.cap = VBYTES * 8;
    ((void (*)(uint64_t, void *, int, int, int, void *))(b + 0x581b60))(it, &w, 1, 1, 0, ctx);
    int len = w.err ? 0 : (int)w.n + (w.bits != 0); return len > 0 && len <= VBYTES ? len : 0;
}
// 读位流建物品（含孔里镶的，照原版复制物品 0x65a840：母件后面紧跟孔里的件数 = 信息块 +0x14），清「新物品」位。room = 落在哪个房间（0 = 不进房间，放进物品栏用）
// Read a bitstream and build the item (including socketed ones, following the original game's item copy 0x65a840: the parent item is immediately followed by the socketed items, count = info block +0x14), clearing the "new item" bit. room = which room it lands in (0 = not placed in a room, used when putting it into an item container)
uint64_t dm_item_make(uint8_t *b, uint64_t G, uint64_t room, const uint8_t *buf, int len, int *socks_out) {
    uint8_t info[0x100]; int used = 0; memset(info, 0, sizeof info);
    ((void (*)(int, const void *, int64_t, int, void *, int))(b + 0x5793a0))(*(uint8_t *)(G + 0x106), buf, len, 1, info, 0x69);
    uint64_t nu = ((uint64_t (*)(uint64_t, uint64_t, const void *, int, int, void *, int *, int))(b + 0x6564e0))(G, room, buf, len, 1, info, &used, 0x69);
    if (!nu) return 0;
    { uint64_t nd = *(uint64_t *)(nu + 0x10); if (nd) *(uint32_t *)(nd + 0x18) &= ~0x2000u; }
    int socks = *(int32_t *)(info + 0x14), off = used, rem = len - used, okn = 0;
    for (int s = 0; s < socks && rem > 0; s++) { uint8_t ci[0x100]; int cu = 0; memset(ci, 0, sizeof ci);
        ((void (*)(int, const void *, int64_t, int, void *, int))(b + 0x5793a0))(*(uint8_t *)(G + 0x106), buf + off, rem, 1, ci, 0x69);
        uint64_t ch = ((uint64_t (*)(uint64_t, uint64_t, const void *, int, int, void *, int *, int))(b + 0x6564e0))(G, 0, buf + off, rem, 1, ci, &cu, 0x69);
        if (!ch || cu <= 0) { LOG("【红】建物品：孔里第 %d 件建不出来", s + 1); break; }
        ((void (*)(uint64_t, int))(b + 0x5ca8a0))(ch, 4);
        if (!((int (*)(uint64_t, uint64_t, uint32_t, uint32_t, int))(b + 0x64d080))(G, nu, *(uint32_t *)(ch + 8), *(uint32_t *)(nu + 8), 2)) LOG("【红】建物品：孔里第 %d 件镶不回去", s + 1); else okn++;
        { uint64_t cd = *(uint64_t *)(ch + 0x10); if (cd) *(uint32_t *)(cd + 0x18) &= ~0x2000u; }
        off += cu; rem -= cu; }
    if (socks_out) *socks_out = okn; return nu;
}
static uint64_t pf_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
void *dm_memfind(const uint8_t *h, size_t hn, const uint8_t *n, size_t nn) { if (!nn || hn < nn) return NULL; for (size_t i = 0; i + nn <= hn; i++) if (h[i] == n[0] && !memcmp(h + i, n, nn)) return (void *)(h + i); return NULL; }
// 工作目录里的原版存档：各角色档 <角色>/<角色>.d2s 与这一类共享仓库档（<名>/<名>.d2i 或 <名>.d2i），整份读进内存，供提交前核对「这件物品在不在原版存档里」
// （编年史仓库 dm_vault.c、个人仓库多页共用）。stash 为空就只读角色档
// The original saves in the working directory: each character save <character>/<character>.d2s and the shared stash saves of this kind (<name>/<name>.d2i or <name>.d2i), read whole into memory for the pre-commit check "is this item in the original save"
// (shared by the chronicle vault dm_vault.c and the multi-page personal stash). If stash is empty, only character saves are read
int dm_save_blobs(const char *wd, const char *stash, dm_blob *out, int max) {
    int k = 0; DIR *dd = opendir(wd); struct dirent *e; char p[1400];
    #define BLOB(path) do { FILE *f_; if (k < max && (f_ = fopen(path, "rb"))) { fseek(f_, 0, SEEK_END); long L_ = ftell(f_); fseek(f_, 0, SEEK_SET); \
        uint8_t *m_ = L_ > 0 && L_ < (16 << 20) ? malloc((size_t)L_) : NULL; size_t n_ = m_ ? fread(m_, 1, (size_t)L_, f_) : 0; fclose(f_); if (m_ && n_) { out[k].d = m_; out[k].n = n_; k++; } else free(m_); } } while (0)
    while (dd && (e = readdir(dd))) { if (e->d_name[0] == '.') continue; snprintf(p, sizeof p, "%s/%s/%s.d2s", wd, e->d_name, e->d_name); BLOB(p); }
    if (dd) closedir(dd);
    if (stash && stash[0]) { snprintf(p, sizeof p, "%s/%s/%s.d2i", wd, stash, stash); BLOB(p); snprintf(p, sizeof p, "%s/%s.d2i", wd, stash); BLOB(p); }
    #undef BLOB
    return k;
}
// 这件（位流）在不在这些存档里：比位流第 8 字节起最多 48 字节（跳过标志位与位置字段，物品挪了地方也认得）。只对有自己身份的物品可靠：
// 简单物品（位流标志位 0x200000，如同种符文、药水）没有唯一编号，两颗同种符文字节一样——调用方别拿它判「同一件」
// Whether this item (bitstream) is in these saves: compare up to 48 bytes of the bitstream starting from byte 8 (skipping the flag bits and position fields, so an item is still recognized after being moved). Reliable only for items with their own identity:
// simple items (bitstream flag bit 0x200000, e.g. runes or potions of the same kind) have no unique id, and two runes of the same kind have identical bytes -- callers must not use it to decide "the same item"
int dm_blobs_have(const dm_blob *bl, int nb, const uint8_t *bits, int len) { int n = len - 8 > 48 ? 48 : len - 8; if (n <= 8) return 0;
    for (int i = 0; i < nb; i++) if (dm_memfind(bl[i].d, bl[i].n, bits + 8, (size_t)n)) return 1; return 0; }
void dm_blobs_free(dm_blob *bl, int nb) { for (int i = 0; i < nb; i++) free(bl[i].d); }
int dm_bits_simple(const uint8_t *bits, int len) { return len >= 4 && (bits[2] & 0x20); }   // 位流标志位 0x200000 = 简单物品（没有唯一编号） | Bitstream flag bit 0x200000 = simple item (no unique id)
// ---------- 个人仓库 2–5 页 ----------
// 角色存档里的个人仓库格（玩家物品栏页 4）放「当前页」；另外 4 页的物品位流放侧文件 <角色>/<角色>.swpage（存档工作目录里，随存档提交原子换入）。
// 翻页（服务端拍）：当前格整页搬出（写位流 → 摘下 0x55e430 → 通知 → 释放）存进当前页的槽，再把目标页逐件建出来（含孔里的）按原格放回（读档放物品 0x6f1940），
// 页码改成目标页，请游戏存盘。同原版共享页：第 1 页往左、第 5 页往右不动。
// 落盘一致性（不变式：同一件物品只在一处——侧文件的页里，或原版存档里）：翻完一次、落盘前不接受下一次翻页，待定的只有「翻页前 / 后」两态。
// 提交前钩子判这次要提交的原版存档（所有角色档 + 共享仓库档）是翻页前还是翻页后，是翻页后才写侧文件。只认「在」做证据（有自己身份的物品，简单物品字节相同认不出是哪一件）：
//   搬进的某件在原版存档里 → 翻页后（翻页前它只在侧文件里）；搬出的某件还在 → 翻页前（翻页后它只在侧文件里，翻页又被挡着回不去）。
//   「不在」不算证据：拖晚的那次序列化可能早于玩家把它放上个人页；搬进的也可能已被挪进共享仓库 / 卖掉（原先按当前角色档多数票，这样就一直判「翻页前」，重进后物品被复制）。
//   都没有证据（翻到空页、简单物品页、搬进的全被挪走或卖掉）：翻页后游戏真的又序列化过（上次存盘时刻 [G+0x5800] 变了），且翻页后已完整提交过两次存盘 → 翻页后。
//   一次存盘提交两回（先角色档、再共享仓库档，宿主实测），所以是翻页后第 4 次提交起；不够就当场再请一次存盘（每次存盘一拍内完成，翻页最多慢几拍）。
//   依据：序列化在翻页前、提交在翻页后的那一次存盘至多一次（同编年史仓库）。
// 进局读侧文件时再兜一道：页里某件（有自己身份的）字节已在原版存档里，就从侧文件里去掉（只会去重）
// ---------- Personal stash pages 2–5 ----------
// The personal stash grid in the character save (player item container page 4) holds the "current page"; the item bitstreams of the other 4 pages go in the sidecar file <character>/<character>.swpage (in the save working directory, swapped in atomically with the save commit).
// Page flip (server tick): the whole current grid is moved out (write bitstream -> remove 0x55e430 -> notify -> free) and stored in the current page's slot, then the target page is built item by item (including socketed ones) and put back in the original cells (load-time item placement 0x6f1940),
// the page number is changed to the target page, and the game is asked to save. Same as the original game's shared pages: nothing happens going left from page 1 or right from page 5.
// On-disk consistency (invariant: the same item exists in only one place -- in a page of the sidecar file, or in the original save): after one flip and before it is persisted, no further flip is accepted, so the pending state is only one of "before the flip / after the flip".
// The pre-commit hook decides whether the original saves about to be committed (all character saves + shared stash saves) are pre-flip or post-flip, and writes the sidecar file only when post-flip. Only "present" counts as evidence (items with their own identity; simple items with identical bytes cannot be told apart):
//   an item moved in is present in the original save -> post-flip (before the flip it was only in the sidecar file); an item moved out is still present -> pre-flip (after the flip it is only in the sidecar file, and flipping is blocked so it cannot go back).
//   "Absent" is not evidence: the delayed serialization may predate the player putting the item on the personal page; an item moved in may also have been moved to the shared stash / sold already (previously this went by majority vote over the current character save, which then kept judging "pre-flip", and items were duplicated after re-entering).
//   No evidence either way (flipped to an empty page, a page of simple items, everything moved in has been moved away or sold): the game has really serialized again after the flip (the last-save time [G+0x5800] changed), and two full saves have been committed after the flip -> post-flip.
//   One save commits twice (first the character save, then the shared stash save; measured on the host), so that means from the 4th commit after the flip; if not there yet, request another save right away (each save completes within one tick, so the flip is at most a few ticks slower).
//   Basis: there is at most one save whose serialization is before the flip and whose commit is after it (same as the chronicle vault).
// One more safeguard when reading the sidecar file on entering a run: if the bytes of an item in a page (with its own identity) are already in the original save, remove it from the sidecar file (this only ever dedups)
#define PGN 5
typedef struct { uint8_t x, y; uint16_t len; uint8_t *bits; } pitem;
typedef struct { int n; pitem *it; } ppage;
static ppage g_pp[PGN]; static int g_pcur, g_pload; static char g_pchar[64];
typedef struct { uint8_t b[56]; uint16_t len; } pfx;   // 待定翻页里一件的位流开头（判在不在原版存档里够用：dm_blobs_have 只看第 8 字节起 48 字节） | The start of one item's bitstream in a pending flip (enough to tell whether it is in the original save: dm_blobs_have only looks at 48 bytes starting from byte 8)
static int g_ppend, g_pdirty; static pfx g_pfx[2][256]; static int g_pfn[2];   // 待定翻页：[0] 搬出的、[1] 搬进的；g_pdirty = 进局去重改了页，下次提交要写 | Pending flip: [0] moved out, [1] moved in; g_pdirty = dedup on entering the run changed a page, to be written on the next commit
static uint64_t g_pmade, g_p5800, g_pG; static int g_pfresh, g_pmore; static char g_pstash[64];   // 翻页时已提交过几次、翻页后 [G+0x5800] 的值、之后看到过真的序列化、钩子要再请一次存盘、共享仓库名 | Number of commits already made at flip time, the value of [G+0x5800] after the flip, a real serialization has been seen since, the hook needs to request another save, shared stash name
uint64_t dm_fs_commit_count(void); const char *dm_stash_name(uint8_t *b);
static _Atomic int g_preq, g_pcur_pub;
int dm_pages_cur(void) { return atomic_load(&g_pcur_pub); }   // 界面读：当前第几页（0 起） | UI read: the current page number (0-based)
int dm_pages_count(void) { return PGN; }
void dm_pages_request_flip(int dir) { atomic_store(&g_preq, dir < 0 ? -1 : 1); }
static void pfp(int k, const uint8_t *bits, int len) { if (g_pfn[k] >= 256 || len <= 0) return; pfx *f = &g_pfx[k][g_pfn[k]++]; memset(f->b, 0, sizeof f->b); memcpy(f->b, bits, (size_t)(len < 56 ? len : 56)); f->len = (uint16_t)(len < 56 ? len : 56); }
static void ppage_free(ppage *p) { for (int i = 0; i < p->n; i++) free(p->it[i].bits); free(p->it); p->n = 0; p->it = NULL; }
static void pages_clear(void) { for (int i = 0; i < PGN; i++) ppage_free(&g_pp[i]); g_pcur = 0; g_ppend = 0; g_pdirty = 0; atomic_store(&g_pcur_pub, 0); }
static void pages_path(char *p, size_t n, const char *wd) { snprintf(p, n, "%s/%s/%s.swpage", wd, g_pchar, g_pchar); }
static void pages_load(void) {   // 进局读一次（工作目录里的那份 = 上次提交的） | Read once on entering a run (the copy in the working directory = the last committed one)
    pages_clear(); const char *wd = dm_save_workdir(); if (!wd || !g_pchar[0]) return;
    char p[1200]; pages_path(p, sizeof p, wd); FILE *f = fopen(p, "rb"); if (!f) { LOG("个人仓库：%s 还没有翻过页（只有第 1 页）", g_pchar); return; }
    uint8_t h[8]; int bad = 0, tot = 0;
    if (fread(h, 1, 8, f) != 8 || memcmp(h, "SWPG", 4) || h[4] != 1 || h[5] >= PGN) bad = 1;
    else { g_pcur = h[5];
        for (int pg = 0; pg < PGN && !bad; pg++) { uint8_t c[2]; if (fread(c, 1, 2, f) != 2) { bad = 1; break; } int n = *(uint16_t *)c; if (!n) continue;
            g_pp[pg].it = calloc((size_t)n, sizeof(pitem)); if (!g_pp[pg].it) { bad = 1; break; }
            for (int i = 0; i < n; i++) { uint8_t e[4]; if (fread(e, 1, 4, f) != 4) { bad = 1; break; } pitem *it = &g_pp[pg].it[i]; it->x = e[0]; it->y = e[1]; it->len = *(uint16_t *)(e + 2);
                if (!it->len || it->len > VBYTES || !(it->bits = malloc(it->len)) || fread(it->bits, 1, it->len, f) != it->len) { bad = 1; break; } g_pp[pg].n++; tot++; } } }
    fclose(f);
    if (bad) { LOG("【红】个人仓库：%s 坏了（读到 %d 件），只用读好的部分", p, tot); }
    { dm_blob bl[64]; int nb = dm_save_blobs(wd, g_pstash, bl, 64), drop = 0;   // 去重：页里有自己身份的那件，字节已在原版存档里 = 复制，从页里去掉（下次提交写回） | Dedup: an item in a page that has its own identity and whose bytes are already in the original save = a duplicate; remove it from the page (written back on the next commit)
      for (int pg = 0; pg < PGN; pg++) for (int i = 0; i < g_pp[pg].n; ) { pitem *it = &g_pp[pg].it[i];
          if (!dm_bits_simple(it->bits, it->len) && dm_blobs_have(bl, nb, it->bits, it->len)) { LOG("【红】个人仓库：第 %d 页格 %d,%d 那件的字节已在原版存档里 → 从侧文件去掉（防复制）", pg + 1, it->x, it->y); free(it->bits); *it = g_pp[pg].it[--g_pp[pg].n]; drop++; tot--; } else i++; }
      dm_blobs_free(bl, nb); g_pdirty = drop > 0; }
    atomic_store(&g_pcur_pub, g_pcur); LOG("个人仓库：%s 当前第 %d 页，另外几页共 %d 件", g_pchar, g_pcur + 1, tot);
}
static int pages_save(const char *wd) {   // 写进工作目录（临时文件再改名）；只在提交前钩子里调 | Write into the working directory (temp file then rename); called only in the pre-commit hook
    char p[1200], t[1210]; pages_path(p, sizeof p, wd); snprintf(t, sizeof t, "%s.tmp", p);
    FILE *f = fopen(t, "wb"); if (!f) return -1;
    uint8_t h[8] = { 'S', 'W', 'P', 'G', 1, (uint8_t)g_pcur, 0, 0 }; int ok = fwrite(h, 1, 8, f) == 8;
    for (int pg = 0; pg < PGN && ok; pg++) { uint16_t n = (uint16_t)g_pp[pg].n; ok = fwrite(&n, 1, 2, f) == 2;
        for (int i = 0; i < n && ok; i++) { pitem *it = &g_pp[pg].it[i]; uint8_t e[4] = { it->x, it->y, 0, 0 }; *(uint16_t *)(e + 2) = it->len; ok = fwrite(e, 1, 4, f) == 4 && fwrite(it->bits, 1, it->len, f) == it->len; } }
    ok = fclose(f) == 0 && ok; if (!ok || rename(t, p)) { remove(t); LOG("【红】个人仓库：写 %s 失败", p); return -1; }
    return 0;
}
static void pages_flip(uint8_t *b, uint64_t G, uint64_t player, int dir) {
    if (g_ppend) { LOG("个人仓库：上一次翻页还没落盘，这次不翻"); return; }
    int tgt = g_pcur + dir; if (tgt < 0 || tgt >= PGN) return;   // 不回绕（同原版共享页） | No wrap-around (same as the original game's shared pages)
    uint64_t t0 = pf_now(); g_pfn[0] = g_pfn[1] = 0;
    uint64_t inv = *(uint64_t *)(player + 0x90), it = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0, list[256]; int n = 0;
    for (int k = 0; it && k < 2048 && n < 256; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 4) list[n++] = it; it = *(uint64_t *)(d + 0xb0); }
    ppage out = { 0, n ? calloc((size_t)n, sizeof(pitem)) : NULL }; static uint8_t buf[VBYTES]; int keep = 0;
    // 先全部写出位流：有一件写不出就整次不翻（不动任何物品）
    // Write out all bitstreams first: if any one item cannot be written, the whole flip is cancelled (no item is touched)
    for (int i = 0; i < n; i++) { uint64_t x = list[i], pp = *(uint64_t *)(x + 0x38); int len = dm_item_write(b, x, buf);
        if (!len || !(out.it[i].bits = malloc((size_t)len))) { LOG("【红】个人仓库：第 %d 件写不出位流，这次不翻", i + 1); out.n = i; ppage_free(&out); return; }
        memcpy(out.it[i].bits, buf, (size_t)len); out.it[i].len = (uint16_t)len; out.it[i].x = (uint8_t)(pp ? *(int32_t *)(pp + 0x10) : 0); out.it[i].y = (uint8_t)(pp ? *(int32_t *)(pp + 0x14) : 0); out.n = i + 1; }
    for (int i = 0; i < n; i++) { uint64_t x = list[i], d = *(uint64_t *)(x + 0x10);
        ((void (*)(uint64_t))(b + 0x55e430))(x); ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, int))(b + 0x642f70))(G, player, x, 0x20, 4, 0, *(uint8_t *)(d + 0x55), out.it[i].y, out.it[i].x); ((void (*)(uint64_t, uint64_t))(b + 0x658be0))(G, x);
        pfp(0, out.it[i].bits, out.it[i].len); }
    ppage left = { 0, g_pp[tgt].n ? calloc((size_t)g_pp[tgt].n, sizeof(pitem)) : NULL };
    for (int i = 0; i < g_pp[tgt].n; i++) { pitem *pi = &g_pp[tgt].it[i]; uint64_t nu = dm_item_make(b, G, 0, pi->bits, pi->len, NULL), nd = nu ? *(uint64_t *)(nu + 0x10) : 0, ip = nu ? *(uint64_t *)(nu + 0x38) : 0;
        if (nd && ip) { *(uint8_t *)(nd + 0x55) = 4; *(int32_t *)(ip + 0x10) = pi->x; *(int32_t *)(ip + 0x14) = pi->y; uint64_t one = nu, ox = 0; ((int (*)(uint64_t, uint64_t, uint64_t *, uint64_t *))(b + 0x6f1940))(G, player, &one, &ox); }
        if (nd && *(uint64_t *)(nd + 0xa0)) { ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70))(G, player, nu, 1, 2, 0, 4, 0, (uint64_t)pi->x | (uint64_t)pi->y << 16); pfp(1, pi->bits, pi->len); free(pi->bits); keep++; }
        else { if (nu) ((void (*)(uint64_t, uint64_t))(b + 0x658be0))(G, nu); left.it[left.n++] = *pi; LOG("【红】个人仓库：第 %d 页第 %d 件放不回格 %d,%d，留在侧文件里", tgt + 1, i + 1, pi->x, pi->y); } }
    free(g_pp[tgt].it); g_pp[tgt] = left;   // 放不回的（不该有）留在目标页槽里，下次翻走时和格里的一起存 | Items that cannot be put back (should not happen) stay in the target page slot and are saved together with those in the grid on the next flip away
    if (g_pp[g_pcur].n) { LOG("【红】个人仓库：第 %d 页槽里有 %d 件放不回的，并进这次搬出的", g_pcur + 1, g_pp[g_pcur].n); pitem *m = realloc(out.it, sizeof(pitem) * (size_t)(out.n + g_pp[g_pcur].n)); if (m) { out.it = m; memcpy(out.it + out.n, g_pp[g_pcur].it, sizeof(pitem) * (size_t)g_pp[g_pcur].n); out.n += g_pp[g_pcur].n; free(g_pp[g_pcur].it); g_pp[g_pcur].it = NULL; g_pp[g_pcur].n = 0; } }
    g_pp[g_pcur] = out; int from = g_pcur; g_pcur = tgt; g_ppend = 1; atomic_store(&g_pcur_pub, g_pcur);
    ((void (*)(uint64_t, int))(b + 0x5cadb0))(player, 1);
    int sv = getenv("DM_TEST_PG_NOSAVE") ? -9 : dm_request_save();   // 测试开关 DM_TEST_PG_NOSAVE=1：翻完不请存盘（配合杀进程验「翻页后、落盘前」掉电） | Test switch DM_TEST_PG_NOSAVE=1: do not request a save after flipping (combine with killing the process to verify power loss "after the page flip, before persisting")
    g_pmade = dm_fs_commit_count(); g_p5800 = *(uint64_t *)(G + 0x5800); g_pfresh = 0; g_pmore = 0;
    LOG("个人仓库：第 %d → %d 页，搬出 %d 件、放回 %d 件，用时 %.2f 毫秒；请游戏存盘 → %d", from + 1, tgt + 1, n, keep, (pf_now() - t0) / 1e6, sv);
}
static void pages_pre_commit(const char *wd) {   // 提交前钩子（持 g_pmu 调），规则见文件头「落盘一致性」 | Pre-commit hook (called holding g_pmu); for the rules see "On-disk consistency" in the file header
    if (!g_pload || !g_pchar[0] || (!g_ppend && !g_pdirty)) return;
    if (!g_ppend) { if (pages_save(wd) == 0) { g_pdirty = 0; LOG("个人仓库：进局去重后的侧文件随这次提交落盘"); } return; }
    dm_blob bl[64]; int nb = dm_save_blobs(wd, g_pstash, bl, 64), post = 0, pre = 0, uniq = 0; uint64_t cno = dm_fs_commit_count() + 1;
    for (int k = 0; k < 2; k++) for (int i = 0; i < g_pfn[k]; i++) { pfx *f = &g_pfx[k][i]; if (dm_bits_simple(f->b, f->len)) continue; uniq++; if (dm_blobs_have(bl, nb, f->b, f->len)) { if (k) post++; else pre++; } }
    dm_blobs_free(bl, nb);
    const char *why = NULL;
    if (pre && post) LOG("【红】个人仓库：提交前核对 —— 搬出的 %d 件还在、搬进的 %d 件也在（不该有），这次不写侧文件", pre, post);
    else if (post) why = "搬进的在原版存档里";
    else if (pre) LOG("个人仓库：提交前核对 #%llu —— 搬出的 %d 件还在 → 角色档是翻页前的，这次不写侧文件（留到下一次）", (unsigned long long)cno, pre);
    else if (!g_pfn[0] && !g_pfn[1]) why = "两页都空，不涉及物品";
    else if ((g_pfresh || (g_pG && *(uint64_t *)(g_pG + 0x5800) != g_p5800 && *(uint64_t *)(g_pG + 0x5800) != 1)) && cno >= g_pmade + 4) why = "没有在场证据（翻到空页 / 简单物品 / 搬进的已被挪走），但翻页后游戏又序列化过、已完整提交过两次存盘";
    else { g_pmore = 1; LOG("个人仓库：提交前核对 #%llu —— 没有在场证据（有身份的 %d 件），等翻页后的存档（%s）", (unsigned long long)cno, uniq, g_pfresh ? "已序列化" : "还没看到序列化"); }
    if (why && pages_save(wd) == 0) { g_ppend = 0; g_pdirty = 0; g_pmore = 0; LOG("个人仓库：提交前核对 #%llu —— %s → 翻页后，侧文件随这次提交落盘（当前第 %d 页）", (unsigned long long)cno, why, g_pcur + 1); }
}
// 测试开关 DM_TEST_PG_SOCK=<拍>（造带孔物品现场）：背包里找一件能镶的装备 + 一颗宝石，用原版镶嵌 0x64d080 镶上，再把母件搬进个人仓库格 (5,5)（写位流 → 摘下 → 释放 → 建物品（含孔里的）→ 读档放物品）
// Test switch DM_TEST_PG_SOCK=<tick> (sets up a socketed-item test case): find a socketable piece of equipment + a gem in the inventory, socket it with the original game's socketing 0x64d080, then move the parent item to personal stash cell (5,5) (write bitstream -> remove -> free -> build item (including socketed ones) -> load-time item placement)
static int item_kids(uint64_t it) { uint64_t inv = *(uint64_t *)(it + 0x90); int n = 0; if (!inv || *(uint32_t *)inv != 0x01020304) return 0; for (uint64_t c = *(uint64_t *)(inv + 0x10); c && n < 16; ) { uint64_t d = *(uint64_t *)(c + 0x10); if (!d) break; n++; c = *(uint64_t *)(d + 0xb0); } return n; }
static void pg_sock_test(uint64_t G, uint64_t player, uint8_t *b, uint32_t tq) {
    static uint32_t at; static int got; if (!got) { got = 1; const char *e = getenv("DM_TEST_PG_SOCK"); at = e ? (uint32_t)atoi(e) : 0; }
    if (!at || tq < at) return; at = 0;
    uint64_t inv = *(uint64_t *)(player + 0x90), it = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0, gems[64], gear[64]; int ng = 0, nq = 0;
    for (int k = 0; it && k < 2048; k++) { uint64_t d = *(uint64_t *)(it + 0x10); if (!d) break; uint32_t c = ((uint32_t (*)(uint64_t))(b + 0x570260))(it);
        if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 0) { if (((char *)&c)[0] == 'g' && ng < 64) gems[ng++] = it; else if (nq < 64) gear[nq++] = it; } it = *(uint64_t *)(d + 0xb0); }
    for (int q = 0; q < nq && ng; q++) { uint64_t g = gems[ng - 1], gd = *(uint64_t *)(g + 0x10), gp = *(uint64_t *)(g + 0x38); int gx = gp ? *(int32_t *)(gp + 0x10) : 0, gy = gp ? *(int32_t *)(gp + 0x14) : 0;
        ((void (*)(uint64_t))(b + 0x55e430))(g); ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, int))(b + 0x642f70))(G, player, g, 0x20, 4, 0, *(uint8_t *)(gd + 0x55), gy, gx);
        ((void (*)(uint64_t, int))(b + 0x5ca8a0))(g, 4);
        if (((int (*)(uint64_t, uint64_t, uint32_t, uint32_t, int))(b + 0x64d080))(G, gear[q], *(uint32_t *)(g + 8), *(uint32_t *)(gear[q] + 8), 2)) {
            ng--; uint64_t x = gear[q], pp = *(uint64_t *)(x + 0x38), d = *(uint64_t *)(x + 0x10); static uint8_t buf[VBYTES]; int len = dm_item_write(b, x, buf); uint32_t c = ((uint32_t (*)(uint64_t))(b + 0x570260))(x);
            LOG("镶孔测试：%.3s 镶上一颗宝石（孔里现 %d 件），位流 %d 字节，搬进个人仓库 (5,5)", (const char *)&c, item_kids(x), len);
            ((void (*)(uint64_t))(b + 0x55e430))(x); ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, int))(b + 0x642f70))(G, player, x, 0x20, 4, 0, *(uint8_t *)(d + 0x55), pp ? *(int32_t *)(pp + 0x14) : 0, pp ? *(int32_t *)(pp + 0x10) : 0); ((void (*)(uint64_t, uint64_t))(b + 0x658be0))(G, x);
            int sk = 0; uint64_t nu = len ? dm_item_make(b, G, 0, buf, len, &sk) : 0, nd = nu ? *(uint64_t *)(nu + 0x10) : 0, ip = nu ? *(uint64_t *)(nu + 0x38) : 0;
            if (nd && ip) { *(uint8_t *)(nd + 0x55) = 4; *(int32_t *)(ip + 0x10) = 5; *(int32_t *)(ip + 0x14) = 5; uint64_t one = nu, ox = 0; ((int (*)(uint64_t, uint64_t, uint64_t *, uint64_t *))(b + 0x6f1940))(G, player, &one, &ox);
                ((void (*)(uint64_t, uint64_t, uint64_t, int, int, int, int, int, uint64_t))(b + 0x642f70))(G, player, nu, 1, 2, 0, 4, 0, 5 | 5 << 16); }
            LOG("镶孔测试：重建后孔里 %d 件，进个人仓库 %s", nu ? item_kids(nu) : -1, nd && *(uint64_t *)(nd + 0xa0) ? "成" : "没成"); return; }
        ((void (*)(uint64_t, int))(b + 0x5ca8a0))(g, 0); }   // 镶不上：这件没孔，换下一件（宝石已摘下，测试用，不放回） | Cannot socket: this item has no sockets, try the next one (the gem is already removed; test only, not put back)
    LOG("镶孔测试：背包里没有能镶的装备（%d 件装备、%d 颗宝石）", nq, ng);
}
void dm_pages_tick(uint64_t G, uint64_t player, uint8_t *b) {   // 服务端拍（diag.c hud_tick）；不在局内 player 为 0 | Server tick (hud_tick in diag.c); player is 0 when not in a run
    static uint64_t lp; if (!player) { lp = 0; g_pload = 0; return; }
    uint32_t tq = *(uint32_t *)(G + 0x170);
    if (player != lp) { lp = player; uint64_t pd = *(uint64_t *)(player + 0x10); pthread_mutex_lock(&g_pmu); strlcpy(g_pchar, pd ? (const char *)pd : "", sizeof g_pchar);
        { const char *sn = dm_stash_name(b); strlcpy(g_pstash, sn ? sn : "", sizeof g_pstash); } pages_load(); g_pload = 1; pthread_mutex_unlock(&g_pmu); }
    { pthread_mutex_lock(&g_pmu); uint64_t v = *(uint64_t *)(G + 0x5800); g_pG = G;   // 翻页后游戏真的序列化过一次：上次存盘时刻 [G+0x5800] 变成了别的值（请存盘时我们拨成 1，游戏存完写当时时刻） | After the page flip the game really serialized once: the last-save time [G+0x5800] changed to another value (we set it to 1 when requesting a save; the game writes the current time after saving)
      if (g_ppend && !g_pfresh && v != g_p5800 && v != 1) g_pfresh = 1;
      int more = g_pmore; g_pmore = 0; pthread_mutex_unlock(&g_pmu); if (more && !getenv("DM_TEST_PG_NOSAVE")) { int r = dm_request_save(); LOG("个人仓库：翻页还没证明，再请一次存盘 → %d", r); } }
    pg_sock_test(G, player, b, tq);
    int dir = atomic_exchange(&g_preq, 0);
    const char *e = getenv("DM_TEST_PG_GO");   // 测试开关 DM_TEST_PG_GO=<拍>:<方向>[,<拍>:<方向>…]：到拍翻页（+1 下一页 / -1 上一页） | Test switch DM_TEST_PG_GO=<tick>:<direction>[,<tick>:<direction>...]: flip the page at that tick (+1 next page / -1 previous page)
    if (e) { char buf[128]; strlcpy(buf, e, sizeof buf); for (char *sv, *t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv)) { int at = 0, dv = 0; if (sscanf(t, "%d:%d", &at, &dv) == 2 && (uint32_t)at == tq) dir = dv; } }
    const char *lg = getenv("DM_TEST_PG_LOG");   // 测试开关 DM_TEST_PG_LOG=<拍>[,<拍>…]：到拍把个人仓库格里每件的物品码 / 格 / 孔里件数打出来（回归比对用） | Test switch DM_TEST_PG_LOG=<tick>[,<tick>...]: at that tick, print the item code / cell / socketed item count of every item in the personal stash grid (for regression comparison)
    if (lg) { char buf[128]; strlcpy(buf, lg, sizeof buf); for (char *sv, *t = strtok_r(buf, ",", &sv); t; t = strtok_r(NULL, ",", &sv)) if ((uint32_t)atoi(t) == tq) {
        uint64_t inv = *(uint64_t *)(player + 0x90), it = inv && *(uint32_t *)inv == 0x01020304 ? *(uint64_t *)(inv + 0x10) : 0; int n = 0;
        for (int k = 0; it && k < 2048; k++) { uint64_t d = *(uint64_t *)(it + 0x10), pp = *(uint64_t *)(it + 0x38); if (!d) break;
            if (*(uint32_t *)(it + 0xc) == 0 && *(uint8_t *)(d + 0x55) == 4) { uint32_t c = ((uint32_t (*)(uint64_t))(b + 0x570260))(it); n++; LOG("个人仓库读数：拍 %u %.3s 格 %d,%d 孔里 %d 件", tq, (const char *)&c, pp ? *(int32_t *)(pp + 0x10) : -1, pp ? *(int32_t *)(pp + 0x14) : -1, item_kids(it)); }
            it = *(uint64_t *)(d + 0xb0); }
        LOG("个人仓库读数：拍 %u 第 %d 页，格里 %d 件", tq, g_pcur + 1, n); } }
    if (dir) { pthread_mutex_lock(&g_pmu); pages_flip(b, G, player, dir); pthread_mutex_unlock(&g_pmu); }
}
static void pages_hook(const char *wd) { pthread_mutex_lock(&g_pmu); pages_pre_commit(wd); pthread_mutex_unlock(&g_pmu); }
__attribute__((constructor)) static void pages_hook_install(void) { extern void (*g_dm_pre_commit_hook[4])(const char *); g_dm_pre_commit_hook[0] = pages_hook; }
