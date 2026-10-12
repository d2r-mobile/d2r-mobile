// 实体手柄四个面键 → 游戏键：按键帽上印的字母对应（提示条写哪个字母就按哪个字母）。纯函数，前端 dm_gamepad.m 用，宿主单测 tests/t_padmap.c 核。
//   系统（GameController）给的四个面键是按位置定的，不看键帽上的字：buttonA 下、buttonB 右、buttonX 左、buttonY 上。
//   · 任天堂手柄（Switch Pro、Joy-Con）：键帽是 右 A、下 B、上 X、左 Y —— 与 Switch 原样一致。
//   · Xbox 手柄：键帽是 下 A、右 B、左 X、上 Y。
//   · 键帽上没有字母的（PS 等）和认不出类型的：按 Xbox 的位置排（下方键 = 确认）。
//   认类型看系统给的产品类别与厂商名两个字符串（不分大小写）里有没有 switch / nintendo / joy-con；都没有 = 按 Xbox 排。
//   （Switch Pro 的厂商名是「Pro Controller」、不带这些词，要靠产品类别「Switch Pro Controller」认；所以两个串都看。）
// The four face buttons of a physical controller -> game keys: mapped by the letter printed on the button glyph (press whichever letter the
// button button legend shows). Pure function, used by the front end dm_gamepad.m, checked by the host unit test tests/t_padmap.c.
//   The four face buttons given by the system (GameController) are defined by position, not by the letter on the button glyph:
//   buttonA bottom, buttonB right, buttonX left, buttonY top.
//   - Nintendo controllers (Switch Pro, Joy-Con): the glyphs are right A, bottom B, top X, left Y -- identical to the Switch as is.
//   - Xbox controllers: the glyphs are bottom A, right B, left X, top Y.
//   - Controllers with no letters on the glyphs (PS etc.) and unrecognized types: laid out by the Xbox positions (bottom button = confirm).
//   The type is recognized by whether the two strings given by the system, product category and vendor name (case-insensitive), contain
//   switch / nintendo / joy-con; neither = laid out as Xbox.
//   (The vendor name of the Switch Pro is "Pro Controller", which has none of these words, so it must be recognized by the product category
//   "Switch Pro Controller"; that is why both strings are checked.)
#pragma once
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include "dm_input.h"
enum { DM_PADKIND_XBOX = 0, DM_PADKIND_NINTENDO = 1 };
static inline int dm_padmap_has(const char *s, const char *w) { size_t n = strlen(w); for (; s && *s; s++) if (!strncasecmp(s, w, n)) return 1; return 0; }
static inline int dm_padmap_kind(const char *category, const char *vendor) {
    static const char *const k[] = { "switch", "nintendo", "joy-con" };
    for (unsigned i = 0; i < sizeof k / sizeof *k; i++) if (dm_padmap_has(category, k[i]) || dm_padmap_has(vendor, k[i])) return DM_PADKIND_NINTENDO;
    return DM_PADKIND_XBOX;
}
static inline uint64_t dm_padmap_face(int kind, int south, int east, int west, int north) {   // 四个参数 = 下 / 右 / 左 / 上那颗按着没有 | The four parameters = whether the bottom / right / left / top button is held
    if (kind == DM_PADKIND_NINTENDO) return (east ? DM_BTN_A : 0) | (south ? DM_BTN_B : 0) | (north ? DM_BTN_X : 0) | (west ? DM_BTN_Y : 0);
    return (south ? DM_BTN_A : 0) | (east ? DM_BTN_B : 0) | (west ? DM_BTN_X : 0) | (north ? DM_BTN_Y : 0);
}
