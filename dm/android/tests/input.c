// 合成输入回归：短点击、持续按键、断开、轴范围与发布/消费并发，不依赖游戏或触屏。
// Synthetic input regression: short taps, held keys, disconnect, axis range and concurrent publication / consumption, without game / touch hardware.
#include "../runtime/input.h"
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); abort(); } } while (0)
typedef struct { dm_android_pad *pad; atomic_int done; } input_job;
static void *publish(void *argument) {
    input_job *job = argument;
    for (int i = 0; i < 50000; i++) {
        int value = i % 1000;
        dm_pad_sample sample = {.lx = value, .ly = -value, .rx = value + 1, .ry = -value - 1, .connected = 1};
        dm_android_pad_publish(job->pad, &sample);
    }
    atomic_store(&job->done, 1); return NULL;
}

int main(void) {
    dm_android_pad *pad = dm_android_pad_create(); CHECK(pad);
    dm_npad_state first = dm_android_pad_read(pad); CHECK(!first.attributes && !first.buttons);
    dm_pad_sample sample = {.buttons = 1, .lx = 100000, .ly = -100000, .connected = 1};
    dm_android_pad_publish(pad, &sample);
    sample.buttons = 0; dm_android_pad_publish(pad, &sample);
    dm_npad_state tap = dm_android_pad_read(pad);
    CHECK(tap.buttons == 1 && tap.lx == 32767 && tap.ly == -32767 && tap.attributes == 1);
    CHECK(tap.sampling > first.sampling && !dm_android_pad_read(pad).buttons);
    sample.buttons = 2; dm_android_pad_publish(pad, &sample);
    CHECK(dm_android_pad_read(pad).buttons == 2 && dm_android_pad_read(pad).buttons == 2);
    sample.connected = 0; dm_android_pad_publish(pad, &sample);
    dm_npad_state disconnected = dm_android_pad_read(pad);
    CHECK(!disconnected.buttons && !disconnected.attributes && !disconnected.lx && !disconnected.ly);
    sample = (dm_pad_sample){.rx = 1, .ry = -1, .connected = 1}; dm_android_pad_publish(pad, &sample);
    input_job job = {.pad = pad, .done = 0};
    pthread_t thread; CHECK(!pthread_create(&thread, NULL, publish, &job));
    unsigned reads = 0;
    do {
        dm_npad_state s = dm_android_pad_read(pad);
        CHECK(s.lx == -s.ly && s.rx == s.lx + 1 && s.ry == -s.rx && s.attributes == 1);
        reads++;
    } while (!atomic_load(&job.done));
    CHECK(!pthread_join(thread, NULL)); CHECK(reads > 0);
    dm_android_pad_destroy(pad);
    puts("PASS: latched quick taps, held keys, disconnect clearing, axis limits, coherent concurrent Npad snapshots");
    return 0;
}
