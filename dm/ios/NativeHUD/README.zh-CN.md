# NativeHUD — 底栏

English: [README.md](README.md)

局内屏幕底部那一条：血球、蓝球、腰带、回城、功能入口、经验条。其中大部分不是外壳自己画的界面，而是游戏原版的 HUD（局内常驻界面）部件，重新排了位置。

## 排布

从左到右：

摇杆区 → 抱着血球的雕像 → 七格 → 抱着蓝球的雕像 → 技能区

中间七格依次是：难度（点开是 1–8 的选择条）、四格腰带、回城、功能入口（开背包等功能页）。七格上方是经验条。

## 怎么做到的

不用 mod，走覆盖层 RomFS（构建时生成、App 自带的一小份资源，游戏读文件时先查它，见 [../README.zh-CN.md](../README.zh-CN.md)「覆盖层 RomFS」）。覆盖层里替换两样东西：

- **手柄版 HUD 布局**。原版布局套上仓库里的结构补丁 `../tools/touchrectv1_patch.json`（补丁格式见 `../tools/layout_patch.py`），再由 `../tools/gen_hud_v4.py` 摆成上面的排布。原版部件只改位置与缩放：血球、蓝球、腰带照旧；回城用的是原版第六个技能钮，功能入口用的是原版「升级」按钮。原版的经验条被挪出屏幕，改由外壳按新的几何来画。
- **一张底图**：石条、七个格框、两侧雕像，球心挖空，让游戏自己画的球液透出来。底图写在一个游戏不再读取的旧贴图路径上，登记在 `dm/data/casc_overlay/borrowed_paths.tsv`。

右侧技能区里，游戏原版的五个技能钮仍在布局里占着位置，外壳用 UIKit 画的技能圈叠在它们上面（`../Sources/DmTouchOverlay.m`）。经验条、难度数字和选择条也由这一层画，素材与几何在构建时生成。

手指点在这些部件上算什么，由 `../Sources/dm_touch_controls.c` 的布局决定（`dm_tc_layout_make`）：它按屏幕高度把点击区换算到同一组七格上，和画面对齐。

## 这个目录里有什么

仓库里只放自己画的东西：

| 文件 | 内容 |
|---|---|
| `touch/close.png` | 面板外框右上角的关闭按钮图，由 `../tools/gen_close_btn.py` 画出 |

从原版衍生的底图、布局、字形都不进仓库。`../run.sh build` 时由 `../tools/gen_assets.py` 从你的游戏文件生成到 `../Gen/assets/NativeHUD/`：

| 产物 | 内容 | 生成脚本 |
|---|---|---|
| `v4/base.png` | 底图 | `../tools/gen_hud_v4.py`（几何与素材来源在 `../tools/hud_v4_design.py`） |
| `v4/hud.json` | 改好的手柄版 HUD 布局 | 同上 |
| `v4/hud_v4_ui/` | UIKit 那一层用的素材与几何 | 同上 |

之后 `../tools/gen_romfs_overlay.py` 把底图和布局写进覆盖层 `../Gen/romfs_overlay`。

底图的素材有两个来源：有 PC 版游戏的解包目录时（环境变量 `DM_PC_DATA`）用它的 2 倍原图；没有就把 Switch 版的同名素材放大，会略糊。放哪、怎么设见 [../../README.zh-CN.md](../../README.zh-CN.md)「从你的游戏文件生成资源」。

## 改了之后怎么检查

```bash
dm/ios/tools/check_hud_gen.sh
```

它检查两件事：这些生成物没有被提交进仓库；本机有游戏文件时，把底栏实际生成一遍，跑不通就报红。本机没有游戏文件时第二项跳过。
