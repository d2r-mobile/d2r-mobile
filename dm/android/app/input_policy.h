// Pure native input readiness policy shared by NativeActivity callbacks and
// host tests. It defines when touch events and timer ticks are allowed.
#pragma once

typedef struct dm_input_policy {
    int resumed;
    int focused;
    int queue;
    int window;
    int touch;
    int timer;
} dm_input_policy;

void dm_input_policy_init(dm_input_policy *policy);
void dm_input_policy_set_resumed(dm_input_policy *policy, int resumed);
void dm_input_policy_set_focused(dm_input_policy *policy, int focused);
void dm_input_policy_set_queue(dm_input_policy *policy, int queue);
void dm_input_policy_set_window(dm_input_policy *policy, int window);
void dm_input_policy_set_touch(dm_input_policy *policy, int touch);
void dm_input_policy_set_timer(dm_input_policy *policy, int timer);
int dm_input_policy_ready(const dm_input_policy *policy);
int dm_input_policy_allows(const dm_input_policy *policy);
