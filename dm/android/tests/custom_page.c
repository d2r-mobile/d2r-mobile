#include "../native/custom_page.h"
#include <assert.h>
#include <stdio.h>

typedef union { uint64_t align; uint8_t bytes[0x90]; } widget;
static int layouts;
static void layout(uint64_t w) { assert(w); layouts++; }
static uint64_t init(widget *w, const char *name, int y, int height) {
    *(const char **)(w->bytes + 8) = name;
    w->bytes[0x50] = w->bytes[0x51] = 1;
    int32_t *r = (int32_t *)(w->bytes + 0x70);
    r[1] = y; r[3] = height;
    return (uint64_t)(uintptr_t)w;
}
int main(void) {
    widget rows = {0}, before = {0}, label = {0}, box = {0}, after = {0};
    uint64_t root = init(&rows, "TouchCustomRows", 0, 720);
    uint64_t children[] = {init(&before, "before", 180, 180),
        init(&label, "TouchCustom_icloud_Text", 360, 180),
        init(&box, "TouchCustom_icloud", 415, 70),
        init(&after, "TouchCustom_q3d_Hint", 640, 70), 0};
    *(uint64_t **)(rows.bytes + 0x58) = children;
    *(uint64_t *)(rows.bytes + 0x60) = 5;
    assert(!dm_android_custom_visible("icloud"));
    assert(dm_android_custom_visible("q3d"));
    assert(dm_android_custom_visible("future_upstream_key"));
    assert(!dm_android_custom_hide_cloud(0, layout));
    children[2] = 0;
    assert(!dm_android_custom_hide_cloud(root, layout));
    assert(layouts == 0 && rows.bytes[0x50]);
    children[2] = (uint64_t)(uintptr_t)&box;
    assert(dm_android_custom_hide_cloud(root, layout));
    assert(!label.bytes[0x50] && !box.bytes[0x50] && !box.bytes[0x51]);
    assert(*(int32_t *)(label.bytes + 0x7c) == 0);
    assert(*(int32_t *)(box.bytes + 0x7c) == 0);
    assert(*(int32_t *)(before.bytes + 0x74) == 180);
    assert(*(int32_t *)(after.bytes + 0x74) == 460);
    assert(*(int32_t *)(rows.bytes + 0x7c) == 540);
    assert(layouts == 4);
    assert(dm_android_custom_hide_cloud(root, layout));
    assert(layouts == 4 && *(int32_t *)(rows.bytes + 0x7c) == 540);
    puts("PASS Android custom page: hidden cloud row, compact geometry, repeat and incomplete-page safety");
}
