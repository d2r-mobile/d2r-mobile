DM_ROOT ?= $(HOME)/dm-work
CFLAGS=-O1 -g -Wall -Wno-unused-function
# 服务层接管 hle_*.c 按通配纳入：新增文件不改这一行（免合并冲突）
# Service-layer takeover hle_*.c files are included by wildcard: adding a file does not touch this line (avoids merge conflicts)
SRC=main.c loader.c hle.c crash.c diag.c thread.c memstat.m dm_cursor.c dm_cursor_item.c dm_world.c dm_textinput.c dm_update.c dm_actions.c dm_namegen.c dm_cloudsync.c dm_custom.c dm_elysium.c dm_bossrush.c dm_pages.c dm_vault.c dm_vault_ui.c dm_kills.c dm_textbox.c dm_textbox_core.c dm_potpick.c dm_lootfilter.c dm_autobuff.c dm_beltauto.c dm_padmode.c dm_pouch.c dm_drophaptic.c dm_freetp.c dm_townwp.c dm_shoprefresh.c dm_runeword.c dm_uiscreen.c dm_dropfilter.c dm_hud.c dm_rift.c dm_rift_ui.c dm_bright.c touch_mac.c romfs_validate.c ../ios/Sources/dm_touch_controls.c $(sort $(wildcard hle_*.c)) front_mac.m app_mac.m ../ios/Sources/dm_gamepad.m entry.S dm_i18n.c
# NVN 空设备 + nn::vi（dm/gfx/）
# 按通配纳入 dm/gfx 与 dm/gfx/nvn 下的源（tests/ metal/ 不进宿主）：新增文件不改这一行
# NVN null device + nn::vi (dm/gfx/)
# Sources under dm/gfx and dm/gfx/nvn are included by wildcard (tests/ and metal/ do not go into the host): adding a file does not touch this line
GFX=$(sort $(wildcard ../gfx/*.c ../gfx/nvn/*.c ../gfx/nvn/*.m ../gfx/nvn/*.S ../gfx/tex/*.c ../gfx/tex/*.m))
GFX_H=$(sort $(wildcard ../gfx/nvn/*.h ../gfx/nvn/include/*.h ../gfx/tex/*.h))
# 第三方：libdeflate 只编解压相关源（TACT BLTE zlib 块，hle_tact.c）
# Third party: only the decompression-related sources of libdeflate are built (TACT BLTE zlib blocks, hle_tact.c)
LIBDEFLATE=../third_party/libdeflate/lib/deflate_decompress.c ../third_party/libdeflate/lib/zlib_decompress.c ../third_party/libdeflate/lib/adler32.c ../third_party/libdeflate/lib/utils.c ../third_party/libdeflate/lib/arm/cpu_features.c
.DEFAULT_GOAL := dmhost   # 下面先写了表头生成规则，缺省目标要明说（否则 make 只生成表头、不编宿主） | The header generation rules are written first below, so the default goal must be stated explicitly (otherwise make only generates the headers and does not build the host)
# 由原版表生成的头文件：不进仓库，构建时从玩家自己的游戏文件（Switch 版 3.3.94012 的 Update/RomFS）生成；路径用环境变量 DM_ROMFS_SRC 指定
# Headers generated from the original game's tables: not checked into the repository, generated at build time from the player's own game files (Update/RomFS of Switch version 3.3.94012); the path is given by the environment variable DM_ROMFS_SRC
ROMFS?=$(or $(DM_ROMFS_SRC),$(DM_ROOT)/data/switch/3.3.94012/Update/RomFS)
GENH=dm_vault_tab.h dm_hud_exp.h dm_bright_levels.inc
NEED_ROMFS=@test -f "$(ROMFS)/data/build_manifest.psv" || { echo "🔴 没找到游戏文件：$(ROMFS)/data/build_manifest.psv。把 Switch 版 3.3.94012 的 Update/RomFS 放到这里，或用环境变量 DM_ROMFS_SRC 指过去（见 dm/README.md「从你的游戏文件生成资源」）"; exit 1; }
dm_vault_tab.h: ../ios/tools/gen_vault_table.py
	$(NEED_ROMFS)
	python3 ../ios/tools/gen_vault_table.py "$(ROMFS)" > $@.tmp && mv $@.tmp $@
dm_hud_exp.h: ../ios/tools/gen_hud_exp.py ../ios/tools/romfs_art.py
	$(NEED_ROMFS)
	python3 ../ios/tools/gen_hud_exp.py "$(ROMFS)" > $@.tmp && mv $@.tmp $@
dm_bright_levels.inc: ../tools/gen_bright_levels.py ../ios/tools/touch_layout.py
	$(NEED_ROMFS)
	python3 ../tools/gen_bright_levels.py "$(ROMFS)" > $@.tmp && mv $@.tmp $@
dmhost: $(SRC) $(GFX) $(LIBDEFLATE) dm.h hle.h dm_input.h front.h touch_mac.h dm_cursor.h dm_i18n_tab.h ../ios/Sources/dm_touch_controls.[ch] $(GFX_H) $(GENH)
	clang $(CFLAGS) -I. -I../gfx/nvn/include -fobjc-arc -o $@ $(SRC) $(GFX) $(LIBDEFLATE) -framework Cocoa -framework QuartzCore -framework Metal -framework AudioToolbox -framework ImageIO -framework UniformTypeIdentifiers -framework GameController
