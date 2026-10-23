// 安卓构建：宿主源码按苹果目标分支的地方一律走「既非 iOS 也非 Mac」。
// Android build: wherever the host source branches on Apple targets, always take "neither iOS nor Mac".
#pragma once
#define TARGET_OS_IPHONE 0
#define TARGET_OS_IOS 0
#define TARGET_OS_OSX 0
#define TARGET_OS_MAC 0
#define TARGET_OS_SIMULATOR 0
#define TARGET_OS_MACCATALYST 0
