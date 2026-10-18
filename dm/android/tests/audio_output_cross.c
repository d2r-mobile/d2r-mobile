// #163 Android cross-link check: real AAudio table plus the composed owner; no device open and no audio execution.
#include "../app/audio_output.h"

int main(void) {
    dm_audio_options options = DM_AUDIO_OPTIONS_INIT;
    options.queue_frames = 4;
    options.api = dm_audio_default_api();
    if (!options.api) return 1;
    dm_audio_output output;
    if (dm_audio_output_init(&output, &options, 0, 0.5f) != 0) return 1;
    // 不创建流：只证明组合 owner 与真实 AAudio 表可交叉链接。| No stream is created; link proof only.
    return dm_audio_output_destroy(&output) == 0 ? 0 : 1;
}
