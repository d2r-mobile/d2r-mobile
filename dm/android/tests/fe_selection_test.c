#include "../../host/fe_selection.h"
#include <stdio.h>

int main(void) {
    dm_fe_selection s;
    dm_fe_selection_reset(&s);
    if (dm_fe_selection_keeps_main(&s)) return 1;
    if (!dm_fe_selection_reopen_after_20(&s, 20)) return 2; // old safe path: no selection -> reopen
    dm_fe_selection_note(&s, "HeroName");
    if (!dm_fe_selection_keeps_main(&s)) return 3;
    if (dm_fe_selection_reopen_after_20(&s, 20)) return 4; // explicit selection -> no auto X
    if (dm_fe_selection_reopen_after_20(&s, 19)) return 5;
    dm_fe_selection_note(&s, "");
    if (dm_fe_selection_keeps_main(&s)) return 6;
    if (!dm_fe_selection_reopen_after_20(&s, 20)) return 7;
    dm_fe_selection_note(&s, "HeroName");
    dm_fe_selection_reset(&s); // lifecycle / new list reset
    if (dm_fe_selection_keeps_main(&s)) return 8;
    if (!dm_fe_selection_reopen_after_20(&s, 20)) return 9;
    puts("PASS: selected character keeps main menu; empty/reset reopens after 20 frames");
    return 0;
}
