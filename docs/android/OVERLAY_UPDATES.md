# APK-bundled interface / APK 内置界面

Players update the APK to receive interface layouts, strings and related resources. There is no separate interface
download or import action. Import the game-data ZIP once; saves, game modules, shaders and launch settings are kept
when installing a newer APK. The launcher prepares its bundled interface after the first game import and whenever
the APK's interface version or content changes. An unchanged bundle is not extracted again.

玩家只需更新 APK，即可更新界面布局、文字和相关资源，不再下载或导入独立界面包。
游戏数据导入一次；覆盖安装 APK 保留游戏本体、着色器、存档及启动设置。
首次导入游戏后和 APK 界面变化时自动准备资源，内容相同时不会重复解压。

The APK is authoritative: an older imported overlay or a previous manual interface update is replaced with the
APK's matching version. Installing an older supported APK also restores its own interface. A bundle for another
game build is rejected before replacement and launch is blocked with an error. The player must use an APK matching
their imported game version. An old interface ZIP cannot be imported as game data; the error explains the new flow.

旧包自带和之前手动更新的界面都由 APK 对应版本替换。
界面与游戏版本不匹配时保留原数据并阻止启动，需使用匹配的 APK。

## Building / 构建

Generate the complete overlay with the existing generator and locally owned original game resources, then pass it
to the APK builder alongside the matching RomFS:

```sh
python3 dm/android/tools/build.py --game-run --version main-COMMIT \
  --interface-overlay /absolute/generated/overlay \
  --interface-romfs /absolute/original/RomFS
```

Other toolchain, Turnip and generated touch-art inputs follow the usual APK build instructions. Playable builds
require both interface inputs so an APK without its interface cannot accidentally ship. The builder creates a
private `assets/interface.zip` inside the APK using `build_overlay_update.py`; this is an implementation detail,
not another player download. Generated game-derived resources remain local and must not be committed or published
as source assets. This workflow does not add original game data to the source repository.

New game-data packages use `build_package.py assemble` without an overlay option. Existing packages with optional
overlays remain readable for migration; after import the APK supplies the interface. Updating layout does not wire
missing host features: auto-buff, swap placement and dynamic-quality feedback require separate changes.

## Format and replacement / 格式与替换

- ZIP first entry: `android-update.properties`, UTF-8 with LF line endings.
- Required fields: `format=1`, `host_contract=1`, `version`, `base_sha256`, `files`, `bytes`, `content_sha256`.
- `base_sha256` hashes the original RomFS `data/build_key.txt` bytes and must match installed data.
- Remaining entries: regular `overlay/...` files from the existing complete-overlay contract; no directory entries,
  duplicate names, traversal, modules or saves. The manifest cannot be supplied again inside `overlay/`.
- `content_sha256` hashes sorted UTF-8 `entry-name<TAB>SHA256(file-bytes)<LF>` records. Counts and byte totals
  must match actual extraction. Limit: 64 files, 2 GiB total and 16 MiB per sidecar; CASC `.data` files share the total limit.
- The app writes and validates `overlay.update`, syncs its files, moves live to `overlay.replaced`, then renames
  the complete staged directory into place. Failed replacement rolls back. A later launcher start recovers an
  interrupted rename and cleans unfinished extraction. Invalid recovery targets retain their backup and block launch.
- After commit, cleanup failure leaves the backup for the next launch. This is process-interruption recovery,
  not a guarantee against arbitrary physical-storage failure or power loss.

先暂存、校验和同步文件，再替换目录；失败回滚。启动页恢复中断状态，发现异常时保留备份并阻止启动。
资源随已签名 APK 发布，内部校验和用于检查完整性与游戏版本匹配。

## Verification / 验证

`OverlayArchiveTest` uses real ZIP streams and temporary directories: success, version reload, corrupt data,
wrong game/host/format, malformed metadata, duplicate/traversal entries, cancellation, read failure, failed rename,
interrupted recovery, and accidental use of the full-package importer. Its Python-builder round trip checks
the actual production builder against the Java importer. Required `java-boundary` CI runs these tests and
compiles the playable launcher against Android API 35. Native ASan/UBSan tests retain unknown-entry rejection.

Bundled-upgrade tests cover legacy migration, unchanged resources, same-version content changes, APK rollback,
wrong-game rejection and damaged metadata. `build_reuse_test.py` opens the archive inside the synthetic APK produced
by the real assembly path. Earlier manual-import device acceptance does not by itself establish automatic APK
upgrade acceptance. Physical power-loss, all language restarts and gameplay automation remain separate coverage.

The Xiaomi15Ultra passed APK overwrite installation with automatic migration from a manually installed interface,
no file-picker interaction, byte-for-byte save preservation before gameplay, unchanged overlay mtime after reopening
the launcher, removal of the manual update entry, and existing-character camp entry with normal save exit.
测试手机已通过覆盖安装后自动迁移、无需选择文件、进入游戏前存档逐文件一致、重复启动不重复写入、
移除手动更新入口及原角色进入营地和正常保存退出检查。
各语言完整重启、真实手柄及实际断电恢复仍需单独验证。
