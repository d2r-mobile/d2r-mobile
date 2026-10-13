# 老机型（A16 及更早的芯片）：贴图包怎么做

English: [LEGACY_DEVICES.md](LEGACY_DEVICES.md)

iPhone 14 系列、iPhone 15 / 15 Plus 以及更早的机型可以玩，只是多一步：要用你自己的游戏文件做一份**老机型贴图包**，放进手机。这一页从头讲到尾。

## 为什么要多这一步

游戏的贴图绝大多数用的是 BC 压缩格式。苹果的显卡从 A17 Pro（iPhone 15 Pro）起才支持这种格式，更早的芯片不认。做法是在电脑上把这些贴图离线转成这些机型支持的 ASTC 格式，画面是同一份画面。贴图是游戏内容，所以这个包不能由我们提供，只能用你自己的游戏文件在你自己的电脑上生成。

**哪些机型需要**：App 启动时自己会判断，不用查表。需要的机型会弹「需要老机型补充包」（补充包就是这里说的贴图包）；如果弹的是「这台设备要多做一步」（旧版本显示的是「暂不支持本机型」），说明这个 App 构建时没有带上贴图包的配套部分——照下面第 4 步重新构建即可。

## 需要什么

- 走「有 Mac」那条路（见首页「开始使用」）：`./setup.sh` 已经跑完。
- 编码器 **astcenc**（ARM 的开源贴图编码器，Apache 2.0 许可）。从源码编一份，约一两分钟：

  ```bash
  brew install cmake
  git clone https://github.com/ARM-software/astc-encoder
  cmake -S astc-encoder -B astc-encoder/b -DASTCENC_ISA_NEON=ON -DCMAKE_BUILD_TYPE=Release
  cmake --build astc-encoder/b -j
  export ASTCENC=$PWD/astc-encoder/b/Source/astcenc-neon
  ```

  我们用的是 v5.2.0。
- 磁盘：约 3 GB（转码的中间文件 1.6 GB，贴图包 1.0 GB）。

## 步骤

在仓库根目录执行，先进入 setup 准备好的 Python 环境。`D` 是你选的输出目录，`R` 是游戏资源目录：

```bash
source ~/dm-work/build/cache/dm/venv/bin/activate
R=~/dm-work/data/switch/3.3.94012/Update/RomFS
D=~/dm-work/out/legacy_pack

python3 dm/tools/astc_pack.py "$R" "$D/work" --preset thorough     # 1 转码，约 8 分钟（Mac 全核）
python3 dm/tools/casc_overlay.py pack "$R" "$D/pack" "$D/work/repl.tsv" 0-8   # 2 写成数据档，约半分钟
python3 dm/tools/astc_pack.py finalize "$D/pack"                   # 3 收尾：算大小与校验值
```

做完后 `$D/pack` 里有：

- `Legacy Device Pack/`：**给手机用的文件夹**（里面是 `manifest.json` 和 `data/00-00000008.data`，约 1.0 GB）。
- `pack.json`：这份贴图包的大小与 SHA-256，下一步构建 App 时会带进去，用来在手机上核对。

4. **带着它构建 App**：

   ```bash
   cd dm/ios && DM_ASTC_PACK="$D/pack" ./run.sh build
   ```

   不带这个变量构建出来的 App，在老机型上只会提示「这台设备要多做一步」（旧版本显示的是「暂不支持本机型」）。

5. **装机**，和平常一样（`./run.sh install` 或用 Xcode）。

6. **把贴图包放进手机**：在「文件」App 里打开「我的 iPhone → D2R Mobile」，把 `Legacy Device Pack` 这个文件夹**整个**拷进去，和游戏资源文件夹放在同一层。用 Finder 也可以（连上手机 → 文件 → D2R Mobile）。然后打开游戏。

## 要注意的

- **贴图包和 App 是配对的**。App 按 `pack.json` 里的 SHA-256 核对手机上的贴图包；重新生成过贴图包，就要用新的 `$D/pack` 重新构建 App。同一份游戏文件、同一版 astcenc、同一档（`--preset thorough`）生成的包是逐字节相同的，所以平时不需要重做。
- 文件夹没拷完、拷错了位置、或者和 App 不配套，App 会在弹框里直接说是哪一种。
- 游戏资源文件夹照常要放（见 [GAME_FILES.zh-CN.md](GAME_FILES.zh-CN.md)）；贴图包是另外加的一个文件夹，不是替换。
- 上面写的是「有 Mac」那条路。不用 Xcode、走壳加做包工具的，做贴图包的三条命令相同，带进安装包的方式不同，见 [NO_XCODE.zh-CN.md](NO_XCODE.zh-CN.md) 里「老机型」一节；Windows 上 astcenc 用上游发布页的现成可执行文件即可。

## 它做了什么（想了解的话）

- 转的是 BC3 格式的贴图，约 2.7 万张，占全部贴图的绝大多数；每张逐块转成 ASTC 4×4，文件大小不变。游戏本身认得 ASTC，所以只是换了格式，没有改游戏的读取方式。
- 其余几种量很小的 BC 格式不在这里转，由程序在这些机型上运行时解开。
- 转码时每张贴图都和原图比过清晰度，结果写在 `$D/work/quality.tsv`；颜色和材质贴图的中位数约 60 dB，肉眼看不出差别。

## 实际情况

最低实测机型是 iPhone 14 Pro Max（A16，6 GB 内存），60 帧运行。这条路在内测阶段由 A16 机型的用户走通过；内测玩家实际在玩的最老机型是 iPhone 13 Pro Max（A15，6 GB 内存），30 帧档。
