# Apple TV（tvOS）

社区移植，作者 [@yinqingyan](https://github.com/yinqingyan)，经作者同意按 MIT 许可贡献。**开发者预览**：在 Apple TV 4K（第三代，A15）实机上到达了职业选择画面；进游戏、手柄全键位、存档都还没验证，维护者没有实机测试过。

Community port by [@yinqingyan](https://github.com/yinqingyan), contributed under the MIT License with the author's consent. **Developer preview**: it reached the character-selection screen on a real Apple TV 4K (3rd generation, A15); entering the game, full controller mapping and saves are not verified yet, and the maintainers have not tested it on real hardware.

## 是什么 / What it is

- 独立的 Xcode 目标 `DmTV`（写在 `dm/ios/project.yml` 里），和 iOS 共用宿主、图形层与手柄适配（`ios/Sources/dm_gamepad.m`）；入口是 `dm/tvos/main.m`，iOS 目标不受影响。
- 只认扩展手柄（Xbox / PlayStation / 8BitDo 等）；Siri 遥控器不是扩展手柄，玩不了。没有触屏、iCloud、资源下载界面。
- A separate Xcode target `DmTV` (in `dm/ios/project.yml`) sharing the host, renderer and gamepad adapter with iOS; entry point `dm/tvos/main.m`; the iOS target is unchanged.
- Extended gamepads only (Xbox / PlayStation / 8BitDo …); the Siri Remote is not an extended gamepad. No touch controls, iCloud or download screens.

## 构建 / Build

```bash
dm/tvos/build.sh                  # 实机 / real Apple TV（在 Xcode 里登录能签 tvOS 的账号 / sign in Xcode with an account that can sign tvOS apps）
DM_TV_NOSIGN=1 dm/tvos/build.sh   # 不签名，只核能不能编过 / unsigned, compile check only
```

`build.sh` 先跑 iOS 的生成步（`ios/run.sh build` 的生成部分），再用 `build_shaders.py` 把 Mac 着色器缓存里的 MSL 编成 tvOS 的 metallib 随包带上；图标由 iOS 图标生成（`gen_icon.py`）。
`build.sh` runs the iOS generation step, compiles the translated MSL from the Mac shader cache into tvOS metallibs with `build_shaders.py` and bundles them; the icon is generated from the iOS icon by `gen_icon.py`.

**只能在实机上跑**：tvOS 模拟器的显卡没有 Metal 4、不是统一内存、参数缓冲只有一档，这个图形层起不来。
**Real hardware only**: the tvOS Simulator GPU has no Metal 4, no unified memory and only tier-1 argument buffers, so this renderer cannot start there.

## 资源放在哪 / Where the data goes

原版资源 9 GB，放不进安装包，要拷进 App 的 Caches 目录（相对 App 数据容器）：
The 9 GB of game data does not fit in the app, so it goes into the app's Caches directory (relative to the app data container):

| 路径 / Path | 内容 / Contents |
|---|---|
| `Library/Caches/D2R/RomFS/data` | 原版档案文件 / original archive files |
| `Library/Caches/D2R/astc/data` | 老机型补充包（A15 不支持 BC 贴图，实机必需）/ Legacy Device Pack (required on A15, which has no BC textures) |
| `Library/Caches/D2R/romfs_overlay_astc` | 补充包配套的覆盖层与 `pack.json` / the pack's overlay and `pack.json` |
| `Library/Caches/D2R/save` | 存档 / saves |

**存档在 Caches 里**：Caches 是系统空间紧张时可以清掉的目录，存档有可能丢。tvOS 没有给 App 的持久大目录，正式的存档做法（例如 iCloud）还没做。
**Saves live in Caches**, which tvOS may purge when storage runs low, so saves can be lost. tvOS gives apps no large persistent directory; a durable save solution (for example iCloud) is not implemented yet.

启动时加环境变量 `DM_TV_SHOT_SECONDS=<秒>` 会把到点后的下一帧存成 `Library/Caches/D2R/frame.png`（诊断用）。
Launching with `DM_TV_SHOT_SECONDS=<seconds>` saves the next presented frame after that time to `Library/Caches/D2R/frame.png` (diagnostics).

## 还没做 / Not done yet

- 进游戏、长时间稳定性、手柄全键位、切后台存盘、音频打断、手柄断线重连都没验证。
- Entering the game, long-session stability, full controller mapping, saving when going to the background, audio interruptions and controller reconnection are unverified.
