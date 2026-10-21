// Android-only front-end selection ownership. The original message
// CharacterSelect:SelectCharacter confirms a character and collapses the list;
// the Android auto path must not reopen it after that explicit selection.
#pragma once
#include <stddef.h>
#include <string.h>

typedef struct {
    int selected;
    char name[17];
} dm_fe_selection;

static inline void dm_fe_selection_reset(dm_fe_selection *s) {
    s->selected = 0;
    s->name[0] = 0;
}

static inline void dm_fe_selection_note(dm_fe_selection *s, const char *name) {
    s->selected = 0;
    s->name[0] = 0;
    if (!name || !name[0]) return;
    size_t n = strlen(name);
    if (n > sizeof s->name - 1) n = sizeof s->name - 1;
    memcpy(s->name, name, n);
    s->name[n] = 0;
    s->selected = 1;
}

static inline int dm_fe_selection_keeps_main(const dm_fe_selection *s) {
    return s->selected && s->name[0] != 0;
}

// The existing Android auto-open path waits 20 frames on the bare main menu.
// An explicit selected character suppresses the reopen; an empty/unknown
// selection keeps the old safe reopen path.
static inline int dm_fe_selection_reopen_after_20(const dm_fe_selection *s, int frames) {
    return frames >= 20 && !dm_fe_selection_keeps_main(s);
}
