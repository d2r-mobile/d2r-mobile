# libdeflate（第三方，MIT 许可证，见 COPYING）
- 上游：https://github.com/ebiggers/libdeflate  提交 92e6a0d（2026-08-22）
- 用途：TACT 存储 BLTE 块的 zlib 解码（宿主 hle_tact.c，经剪口在 main 0x1b0ee60 入口接管）
- 平台线 10-01 离线对拍：选角前真被解压的 4,979 个文件全部 MD5 == 清单 CKey；同机比游戏自带 zlib 1.2.11 快 2.42×
- 只编解压相关源：lib/deflate_decompress.c、lib/zlib_decompress.c、lib/adler32.c、lib/utils.c、lib/arm/cpu_features.c
- 未改动上游代码
