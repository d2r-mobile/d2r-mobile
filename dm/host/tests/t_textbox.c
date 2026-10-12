// dm_textbox_core 单测：期望全部手写。U+E07E = EE 81 BE。
// 判据：表里的注释版整串 / 子串换成文字框版；表外的串（含原版自带的带码串、不带码的串）逐字节不动；没有表时一律不动。
// dm_textbox_core unit test: all expectations are hand-written. U+E07E = EE 81 BE.
// Criterion: whole strings / substrings of the annotated version found in the table are replaced by the text box version; strings outside the table (including the original game's own coded strings and strings without codes) are untouched byte for byte; with no table nothing is touched.
#include "../dm_textbox_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define C "\xee\x81\xbe"
static int fails, checks;
static void expect(const char *what, const char *in, int want_k, const char *want) {
    char *o; size_t on; int k = dm_textbox_rewrite(in, strlen(in), &o, &on); checks++;
    int ok = k == want_k && (want ? (o && on == strlen(want) && !memcmp(o, want, on)) : !o);
    if (!ok) { fails++; printf("✗ %s：得到 k=%d「%.*s」\n", what, k, o ? (int)on : 0, o ? o : ""); } else printf("✓ %s\n", what);
    free(o);
}
int main(void) {
    char path[] = "/tmp/t_textbox_XXXXXX"; int fd = mkstemp(path); FILE *f = fdopen(fd, "w");
    fprintf(f, "# 注释版\t文字框版\n");
    fprintf(f, "%s\t%s\n", C "5[强伤60-70%]\\n" C "4死亡之铲", "死亡之铲");   // 范围行 + 名字 | range line + name
    fprintf(f, "%s\t%s\n", C "1怀特之脚", "怀特之脚");                         // 红叶名字色 | Hongye mod name color
    fclose(f);
    expect("没有表：带码串不动", C "1怀特之脚", 0, NULL);
    if (dm_textbox_load(path) != 2) { printf("✗ 读表条数不是 2\n"); return 1; }
    expect("表里的注释版整串 → 文字框版", C "5[强伤60-70%]\n" C "4死亡之铲", 1, "死亡之铲");
    expect("红叶名字色整串 → 原名", C "1怀特之脚", 1, "怀特之脚");
    expect("被拼进长句的注释版只换那一段", "发现编年史物品：" C "1怀特之脚" "！", 2, "发现编年史物品：怀特之脚！");
    expect("同一句里出现两次都换", C "1怀特之脚" "和" C "1怀特之脚", 2, "怀特之脚和怀特之脚");
    expect("原版自带的带码串（悄悄话）不在表里：不动", C "0你悄悄地对" C "1某人" C "0说: 你好", 0, NULL);
    expect("不带码的串：不动", "死亡之铲", 0, NULL);
    expect("多行原版文字（不带码）：不动", "第一行\n第二行", 0, NULL);
    expect("像注释但表里没有的：不动", C "5[强伤1-2%]\n" C "4死亡之铲", 0, NULL);
    unlink(path); dm_textbox_reset();
    expect("清表后：不动", C "1怀特之脚", 0, NULL);
    printf("TEXTBOX %s checks=%d failures=%d\n", fails ? "FAIL" : "PASS", checks, fails);
    return fails != 0;
}
