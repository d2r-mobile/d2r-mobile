// nn::codec::HardwareOpusDecoder：真机经 hwopus 服务用硬件解码（游戏的音频与过场音轨都是 Opus，不是联网功能）。
// 宿主：构造函数改为在同一块内存上构造 SDK 的软件 OpusDecoder（sdk 自带 libopus，ARM 原生）。
// 依据：两者同为 nn::codec::IOpusDecoder 的实现，虚表 10 个槽位顺序与签名逐一相同（析构×2、GetWorkBufferSize、IsInitialized、
// Initialize、Finalize、GetSampleRate、GetChannelCount、Reset、DecodeInterleaved）；游戏按 0x40 字节分配，软件版对象 0x30 字节。
// nn::codec::HardwareOpusDecoder: the real device decodes in hardware via the hwopus service (the game's audio and cutscene tracks are all Opus; this is not an online feature).
// Host: the constructor instead constructs the SDK's software OpusDecoder in the same memory (the sdk bundles libopus, native ARM).
// Basis: both are implementations of nn::codec::IOpusDecoder, with the 10 vtable slots identical one by one in order and signature (destructor×2, GetWorkBufferSize, IsInitialized,
// Initialize, Finalize, GetSampleRate, GetChannelCount, Reset, DecodeInterleaved); the game allocates 0x40 bytes and the software object is 0x30 bytes.
#include "hle.h"

HLE_FORCE("_ZN2nn5codec19HardwareOpusDecoderC1Ei", "hwopus 服务；换成 SDK 软件 OpusDecoder（同一 IOpusDecoder 虚接口）",
          void, h_codec_HwOpusCtor, void *self, int option) {
    (void)option;
    static void (*soft_ctor)(void *);
    if (!soft_ctor && !(soft_ctor = guest_sym("_ZN2nn5codec11OpusDecoderC1Ev"))) die("缺 OpusDecoder 构造");
    soft_ctor(self);
}
