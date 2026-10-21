# 诊断 APK 干净构建与旁车验证（#135）

本文记录一次**实际**
的 owned 诊断 APK 打包与 `app/provenance.py` 旁车验证，用于补齐 #131/PR132 只以自有 zip 验证追溯工具的缺口。构建在独立工作树（已验收提交 `source snapshot`，工作树干净）与隔离的 `DM_ROOT` 中执行：该目录同时承载 `build/`、`output/`、`tmp/` 与 `keys/`，调试签名材料由原构建脚本在该隔离目录**新生成**；未读取既有密钥，未写入共享 APK、release-candidate 或默认根目录。SDK 与 NDK 以只读方式通过 `ANDROID_SDK_ROOT` / `ANDROID_NDK_HOME` 使用。

## 构建入口

```sh
# DM_ROOT 指向隔离目录；host 构建仅需产物导出器时先配置缓存，避免重复整套测试
cmake -S dm/android -B "$DM_ROOT/build/host-sanitized" -DCMAKE_BUILD_TYPE=Debug \
      -DDM_ANDROID_REWRITE_SCAN=OFF -DDM_ANDROID_VULKAN=OFF \
      -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'
cmake --build "$DM_ROOT/build/host-sanitized" --target dm_artifact_test --parallel
bash dm/android/app/build.sh
```

脚本依次执行：NDK/API29 交叉编译 → `dm_artifact_test --export` 自有 fixture → 打包自有着色器 → `aapt2` link → `zip -0` 存储 native 库 → `zipalign -P 16` → 新调试密钥 `apksigner sign` → `apksigner verify` → `zipalign -c -P 16` → `provenance.py write` 写旁车 → `aapt2 dump badging`。所有退出码为 0。

## 本次产物事实

| 项 | 值 |
|---|---|
| APK | 1,917,600 字节，sha256 `b7ea280219610a83247f814c53d5464f7655661ec06593c8448e51d25e8aa1b5` |
| 旁车 | `d2r-diagnostic-debug.apk.provenance.json`，sha256 `c03f44bbf1cfa6f924a6e545c6002a0b0005504cf1a7ad6064d338abc3c3430b` |
| 源码 | commit `source snapshot`，`dirty=false`（clean） |
| 构建事实 | ABI `arm64-v8a`，`platform_api=29`，`min_sdk=29`，`target_sdk=35`，build-tools `35.0.0`，`cmake_build_type=Release` |
| 入口（旁车记录，独立 `unzip -p | sha256` 复核一致） | `lib/arm64-v8a/libdm_android_app.so` 1,891,896 B `c6abfa49…74942f`；`assets/synthetic.dma` 49,360 B `222b4919…ef2e13`；`assets/indexed_textured.vert.spv` 748 B `509e5da6…1a3004`；`assets/indexed_textured.frag.spv` 500 B `32b3aecb…c6ba66` |

## 验证命令与结果

| 检查 | 命令 | 结果 |
|---|---|---|
| 签名 | `apksigner verify --verbose <apk>` | 退出 0，输出 `Verifies` |
| 清单/ABI | `aapt2 dump badging <apk>` | `package org.d2rmobile.android.diagnostic`，`targetSdkVersion 35`，`native-code: 'arm64-v8a'` |
| 16 KiB 对齐 | `zipalign -c -P 16 -v 4 <apk>` | 退出 0，`Verification succesful`（工具原文拼写） |
| 旁车一致 | `python3 dm/android/app/provenance.py verify --apk <apk> --sidecar <sidecar>` | 退出 0，`provenance sidecar verified` |
| 篡改 A：尾部追加 1 字节 | 同上，对副本执行 | 退出 1，`provenance error: sidecar APK size does not match` |
| 篡改 B：`assets/synthetic.dma` 改 1 字节，且把副本旁车的整体 size/sha256/entry_count 改成新 APK 的值 | 同上，对副本执行 | 退出 1，`provenance error: APK entry digest mismatch: assets/synthetic.dma` |

篡改 B 说明旁车不是只比整体哈希：即使整体摘要被同步改写，逐入口摘要仍会拒绝被替换的自有 fixture。旁车输出不含绝对路径（`grep` 检查为 0 处）。

工具版本：aapt2 `2.19-11948202`、apksigner `0.9`、OpenJDK `27`、Python `3.14.7`、CMake `4.4.3`、NDK `29.0.14206865`、SDK build-tools `35.0.0` / platform `android-35`。

## 边界

- ZIP 的 16 KiB 对齐只说明归档内入口偏移，**不等于**每个 native ELF LOAD 段在设备页大小下可加载或兼容；未做 ELF 段级检查，也未在手机上安装、启动或截图。
- 旁车的整体 APK 哈希覆盖整个文件的字节（包括签名相关内容），另单列四个自有入口摘要；这不等于验证签名证书的信任关系，也不构成可复现构建证明或对构建机未受篡改的证明。
- 本次未执行游戏/SDK/rtld，未传输或发布 APK、密钥与私有日志；未修改全局 SDK 或系统设置。
- 构建使用隔离 `DM_ROOT` 中新生成的调试密钥；该私有目录中的签名材料不提交到仓库，也不参与发布。
