// 手机出图几何：保持 16:9 下限/兼容项，按窗口宽高比在 720 级高度上给出更宽的游戏画面。
// Phone output geometry: keep the 16:9 floor/compatibility path; for wider surfaces return a wider
// game image at 720-level height, bounded so fullscreen never means native 3200x1440.
#pragma once

#include <stdint.h>

#define DM_GAME_OUTPUT_BASE_HEIGHT 720u
#define DM_GAME_OUTPUT_MAX_WIDTH 1920u

// Returns 1 and fills w/h for a landscape surface wider than 16:9 (within a small tolerance);
// returns 0 when the caller should keep its existing 16:9 constants (invalid/portrait/16:9).
static inline int dm_game_output_phone(unsigned surface_w, unsigned surface_h, unsigned *w, unsigned *h) {
    // 极端/损坏的窗口尺寸不进计算；真实手机远小于此上界。
    // Extreme/corrupt window sizes never enter the calculation; real phones are far below this.
    if (surface_w < 320 || surface_h < 240 || surface_w > 16384 || surface_h > 16384 || surface_w <= surface_h) return 0;
    // 16:9 容差 1% 用整数交叉相乘，避免浮点转换溢出。
    // The 1% tolerance around 16:9 uses integer cross multiplication, avoiding float-cast overflow.
    if ((uint64_t)surface_w * 9u * 100u <= (uint64_t)surface_h * 16u * 101u) return 0;
    // 先在有界整数域计算并 clamp，再转 unsigned。
    // Compute and clamp in bounded integer arithmetic before converting to unsigned.
    uint64_t width = ((uint64_t)surface_w * DM_GAME_OUTPUT_BASE_HEIGHT + surface_h / 2u) / surface_h;
    if (width < 1280) width = 1280;
    if (width > DM_GAME_OUTPUT_MAX_WIDTH) width = DM_GAME_OUTPUT_MAX_WIDTH;
    width &= ~(uint64_t)3u;
    *w = (unsigned)width;
    *h = DM_GAME_OUTPUT_BASE_HEIGHT;
    return 1;
}
