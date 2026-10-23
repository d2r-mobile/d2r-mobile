# Independent interface updates / 独立界面资源更新

The launcher offers **Update interface resources** under the game-data section. First install an APK supporting
this entry, then select an update ZIP generated for your own game build. Close the game before updating.
The new overlay takes effect on the next launch. The entry displays the installed update version; older overlays
without update metadata are labelled as having arrived with the game data.

启动页游戏数据区域提供“更新界面资源”。先安装支持此入口的 APK，再选择为自己游戏版本生成的界面更新 ZIP。
更新前退出游戏，下次启动生效。入口显示已安装版本；旧覆盖层显示“随游戏数据包导入”。

Only `d2r-package/overlay` is replaced. Modules, RomFS, shader caches, the full-package completion marker,
launch preferences and saves are not rewritten. Importing a new full game-data package still replaces or retires
its optional overlay according to that package; it does not preserve an unrelated interface update.

只替换界面覆盖层，不重写游戏本体、着色器、整包就绪标记、启动设置或存档。
重新导入完整游戏包时，其自带覆盖层仍按整包内容替换或移除，不会强留旧界面版本。

## Build an update / 生成更新包

Generate the complete overlay with the existing generator and the player's original game data, then package it:

```sh
python3 dm/android/tools/build_overlay_update.py OVERLAY_DIRECTORY ROMFS_DIRECTORY interface.zip VERSION
```

`VERSION` accepts 1–80 ASCII letters/digits/dots/underscores/hyphens, beginning with a letter or digit.
The tool reuses the existing complete-overlay validator and includes only overlay resources. It does not generate
or distribute original game data. Keep generated resources local; do not commit or publish copyrighted resources.
Updating the layout does not implement missing Android host features: auto-buff, skill-switch placement and
dynamic-quality feedback still require their own wiring and verification.

生成器复用原有完整覆盖层校验；更新 ZIP 仅包含界面资源。布局升级不能代替尚缺的 Android 功能接线。

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
校验和用于检查完整性与版本匹配，不是发布者数字签名；只导入自己生成或信任来源的更新包。

## Verification / 验证

`OverlayArchiveTest` uses real ZIP streams and temporary directories: success, version reload, corrupt data,
wrong game/host/format, malformed metadata, duplicate/traversal entries, cancellation, read failure, failed rename,
interrupted recovery, and accidental use of the full-package importer. Its Python-builder round trip checks
the actual production builder against the Java importer. Required `java-boundary` CI runs these tests and
compiles the playable launcher against Android API 35. Native ASan/UBSan tests retain unknown-entry rejection.

The Xiaomi 15 Ultra passed actual file-picker import, invalid-ZIP rejection with the existing version/start button
preserved, subsequent successful updates, version persistence and game/camp entry. The Custom page scrolls to both
skill-switch-position choices, hides iCloud and displays all four language choices; compact labels avoid Spanish/Russian
wrapping. Existing saves load and normal app-menu exit writes the save. Full language restarts, physical controllers,
power-loss recovery and gameplay automation conditions remain separate coverage. Local tests cover interrupted updates.

测试手机已验证文件选择更新、错误 ZIP 拒绝并保留原版本及启动能力、后续成功更新、版本保留与营地进入。
新菜单滚动、四语言显示、换组位置行及 iCloud 隐藏已检查；原存档可加载，使用应用菜单正常保存退出。
各语言完整重启、真实手柄及实际断电恢复仍需单独验证。
