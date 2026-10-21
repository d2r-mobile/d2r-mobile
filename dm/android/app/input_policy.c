#include "input_policy.h"

void dm_input_policy_init(dm_input_policy *policy) {
    if (!policy) return;
    policy->resumed = 0;
    policy->focused = 0;
    policy->queue = 0;
    policy->window = 0;
    policy->touch = 0;
    policy->timer = 0;
}

void dm_input_policy_set_resumed(dm_input_policy *policy, int resumed) {
    if (policy) policy->resumed = resumed != 0;
}

void dm_input_policy_set_focused(dm_input_policy *policy, int focused) {
    if (policy) policy->focused = focused != 0;
}

void dm_input_policy_set_queue(dm_input_policy *policy, int queue) {
    if (policy) policy->queue = queue != 0;
}

void dm_input_policy_set_window(dm_input_policy *policy, int window) {
    if (policy) policy->window = window != 0;
}

void dm_input_policy_set_touch(dm_input_policy *policy, int touch) {
    if (policy) policy->touch = touch != 0;
}

void dm_input_policy_set_timer(dm_input_policy *policy, int timer) {
    if (policy) policy->timer = timer != 0;
}

int dm_input_policy_ready(const dm_input_policy *policy) {
    return policy && policy->resumed && policy->focused && policy->queue &&
           policy->window && policy->touch;
}

int dm_input_policy_allows(const dm_input_policy *policy) {
    return dm_input_policy_ready(policy) && policy->timer;
}
