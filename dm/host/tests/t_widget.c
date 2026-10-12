// 控件类型核对（dm_widget.h）的单测，秒级。假「主模块」是一块内存，控件第一个字 = 基址 + 虚表偏移。
// Unit test for the widget type check (dm_widget.h), runs in seconds. The fake "main module" is a block of memory; a widget's first word = base + vtable offset.
#include "dm_widget.h"
#include <stdio.h>

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("  ✗ "); printf(__VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    static uint8_t base[16]; static uint64_t w_toggle[4], w_text[4], w_slot[4], w_slotb[4], w_slotc[4], w_other[4];
    w_toggle[0] = (uint64_t)(uintptr_t)base + DM_VT_TOGGLE; w_text[0] = (uint64_t)(uintptr_t)base + DM_VT_TEXT; w_other[0] = (uint64_t)(uintptr_t)base + 0x1234;
    w_slot[0] = (uint64_t)(uintptr_t)base + DM_VT_SLOT; w_slotb[0] = (uint64_t)(uintptr_t)base + DM_VT_SLOT_B; w_slotc[0] = (uint64_t)(uintptr_t)base + DM_VT_SLOT_C;
    uint64_t tg = (uint64_t)(uintptr_t)w_toggle, tx = (uint64_t)(uintptr_t)w_text, ot = (uint64_t)(uintptr_t)w_other;

    CHECK(dm_widget_vt(base, tg) == DM_VT_TOGGLE, "勾选框的虚表偏移应读到 %#x，读到 %#llx", DM_VT_TOGGLE, (unsigned long long)dm_widget_vt(base, tg));
    CHECK(dm_widget_is(base, tg, DM_VT_TOGGLE), "勾选框应认作勾选框");
    CHECK(!dm_widget_is(base, tx, DM_VT_TOGGLE), "文字框不该认作勾选框（同名控件换了类时就是这种情况）");
    CHECK(dm_widget_is(base, tx, DM_VT_TEXT), "文字框应认作文字框");
    CHECK(!dm_widget_is(base, ot, DM_VT_TOGGLE) && !dm_widget_is(base, ot, DM_VT_TEXT), "不认识的类一律不认");
    CHECK(!dm_widget_is(base, 0, DM_VT_TOGGLE) && dm_widget_vt(base, 0) == 0, "空控件：不认、虚表读 0");
    CHECK(!dm_widget_is(NULL, tg, DM_VT_TOGGLE) && dm_widget_vt(NULL, tg) == 0, "没有基址：不认、虚表读 0");
    CHECK(dm_widget_is_slot(base, (uint64_t)(uintptr_t)w_slot) && dm_widget_is_slot(base, (uint64_t)(uintptr_t)w_slotb) && dm_widget_is_slot(base, (uint64_t)(uintptr_t)w_slotc), "三种物品格都认作物品格");
    CHECK(!dm_widget_is_slot(base, tg) && !dm_widget_is_slot(base, 0), "勾选框 / 空控件不是物品格");
    // 各类的虚表值两两不同（抄错一个数会让两类互认）
    // Vtable values of the classes are pairwise distinct (one mistyped number would make two classes match each other)
    { const uint64_t v[] = { DM_VT_TEXT, DM_VT_INPUT, DM_VT_NUMINPUT, DM_VT_TOGGLE, DM_VT_TABBAR, DM_VT_SLOT, DM_VT_SLOT_B, DM_VT_SLOT_C, DM_VT_IMAGE, DM_VT_BUTTON }; int dup = 0;
      for (unsigned i = 0; i < sizeof v / sizeof *v; i++) for (unsigned j = i + 1; j < sizeof v / sizeof *v; j++) dup += v[i] == v[j];
      CHECK(dup == 0, "虚表常量有 %d 对重复", dup); }

    printf("t_widget：%d 项，%d 项不通\n", checks, fails); return fails != 0;
}
