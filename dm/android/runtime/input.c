// 复用上游输入语义：按下沿锁存、连接属性和采样号；单锁保护整份状态，避免轴来自不同帧。
// Preserve upstream input semantics: latched press edges, connection attributes and sampling IDs; one lock protects whole snapshots against torn axes.
#include "input.h"
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct dm_android_pad { pthread_mutex_t mutex; dm_pad_sample current; uint64_t pressed, sampling; };
static int32_t axis(int32_t value) { return value > 32767 ? 32767 : value < -32767 ? -32767 : value; }

dm_android_pad *dm_android_pad_create(void) {
    dm_android_pad *pad = calloc(1, sizeof *pad);
    if (!pad) return NULL;
    int error = pthread_mutex_init(&pad->mutex, NULL);
    if (error) { free(pad); errno = error; return NULL; }
    return pad;
}

void dm_android_pad_publish(dm_android_pad *pad, const dm_pad_sample *sample) {
    pthread_mutex_lock(&pad->mutex);
    if (!sample->connected) { memset(&pad->current, 0, sizeof pad->current); pad->pressed = 0; }
    else {
        pad->pressed |= sample->buttons & ~pad->current.buttons;
        pad->current = (dm_pad_sample){.buttons = sample->buttons, .lx = axis(sample->lx), .ly = axis(sample->ly),
            .rx = axis(sample->rx), .ry = axis(sample->ry), .connected = 1};
    }
    pthread_mutex_unlock(&pad->mutex);
}

dm_npad_state dm_android_pad_read(dm_android_pad *pad) {
    pthread_mutex_lock(&pad->mutex);
    dm_npad_state state = {.sampling = (int64_t)(++pad->sampling & INT64_MAX),
        .buttons = pad->current.buttons | pad->pressed,
        .lx = pad->current.lx, .ly = pad->current.ly, .rx = pad->current.rx, .ry = pad->current.ry,
        .attributes = pad->current.connected ? 1u : 0u};
    pad->pressed = 0;
    pthread_mutex_unlock(&pad->mutex);
    return state;
}

void dm_android_pad_destroy(dm_android_pad *pad) { pthread_mutex_destroy(&pad->mutex); free(pad); }
