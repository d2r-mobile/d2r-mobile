#!/usr/bin/env python3
# 免费的开发者账号（个人团队）用的工程描述：从 project.yml 去掉它签不了、用不上的东西，写出 project_free.yml（run.sh 在 DM_FREE_ACCOUNT=1 时调）。
# 去掉的：
#   · 六行权限：iCloud 三项、持续执行、增加内存上限、扩展虚拟寻址——免费账号签不了；
#   · 资源下载扩展（RomfsDownloader 目标、主程序对它的依赖、Info.plist 里托管资源包的三个键）——它只服务「苹果托管资源包」，
#     那要通过 TestFlight / 商店分发才有；自己签名装机用不上，却要多占一个应用标识（免费账号 7 天内能新建的标识有上限）。
#     去掉后资源照旧走「拷进 App 的文件夹」这条路。
# 每一类去掉了几处都核对，对不上就报错不出文件（工程描述改了格式时别悄悄漏删）。
# Project description for a free developer account (personal team): removes from project.yml what it cannot sign or has no use for, and writes project_free.yml (called by run.sh when DM_FREE_ACCOUNT=1).
# Removed:
#   · six entitlement lines: the three iCloud items, sustained execution, increased memory limit, extended virtual addressing -- a free account cannot sign them;
#   · the asset download extension (the RomfsDownloader target, the main app's dependency on it, the three Info.plist keys for hosted asset packs) -- it only serves "Apple-hosted asset packs",
#     which exist only when distributing through TestFlight / the store; it is of no use when signing and installing yourself, yet takes up one more App ID (a free account has a cap on how many IDs it can create within 7 days).
#     After removal, assets still go through the "folder copied into the App" path as before.
# The number of places removed in each category is checked, and on a mismatch it reports an error and produces no file (so that nothing is silently left unremoved when the project description changes format).
import re, sys
src, dst = sys.argv[1], sys.argv[2]
L = open(src, encoding='utf-8').read().split('\n')
ENT = re.compile(r'^ *com\.apple\.developer\.(kernel\.increased-memory-limit|kernel\.extended-virtual-addressing|sustained-execution|icloud-container-identifiers|icloud-services|ubiquity-container-identifiers):')
BA = re.compile(r'^ *(BAAppGroupID|BAHasManagedAssetPacks|BAUsesAppleHosting):')
out, n_ent, n_ba, n_dep, n_tgt, i = [], 0, 0, 0, 0, 0
while i < len(L):
    l = L[i]
    if ENT.match(l): n_ent += 1; i += 1; continue
    if BA.match(l): n_ba += 1; i += 1; continue
    if re.match(r'^ *- target: RomfsDownloader\b', l): n_dep += 1; i += 1; continue
    if re.match(r'^  RomfsDownloader:\s*$', l):   # 整个目标块：到下一个同级键（两格缩进）为止 | the whole target block: up to the next key at the same level (two-space indent)
        n_tgt += 1; i += 1
        while i < len(L) and not re.match(r'^  \S', L[i]) and not re.match(r'^\S', L[i]): i += 1
        continue
    out.append(l); i += 1
if (n_ent, n_ba, n_dep, n_tgt) != (6, 3, 1, 1): sys.exit('project_free.py：该去掉的东西数目不对（权限 %d / 6、托管资源包键 %d / 3、依赖 %d / 1、扩展目标 %d / 1）——工程描述的写法变了，先改这个脚本' % (n_ent, n_ba, n_dep, n_tgt))
if any('RomfsDownloader' in l and not l.lstrip().startswith('#') for l in out): sys.exit('project_free.py：去掉之后工程描述里还有 RomfsDownloader')
open(dst, 'w', encoding='utf-8').write('\n'.join(out))
