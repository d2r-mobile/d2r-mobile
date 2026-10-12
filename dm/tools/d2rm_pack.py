#!/usr/bin/env python3
# 不用 Xcode 出可玩的安装包：把你自己的游戏文件做进「壳」。
#   壳（D2RMobile-shell.ipa）是一个不含任何游戏内容、没有签名的安装包；本工具用你的游戏文件在你的电脑上生成它缺的全部内容
#   （改写后的游戏程序、着色器库、覆盖层、界面素材、几张数据表），放进去，写出一个新的安装包。之后用自签工具整包签名装机。
#   游戏文件只在本机读，不上传；做出来的安装包里有游戏内容，请只自己用，不要分发。
# 用法：d2rm_pack.py <壳.ipa> <游戏文件目录> <输出.ipa>
#   游戏文件目录 = Switch 版 3.3.94012 的 Update 目录，里面有 ExeFS/{main,sdk} 与 RomFS/data/…（怎么从自己的游戏导出）
#   工作目录（缓存与中间产物，约 12 GB）：环境变量 DM_ROOT，缺省 ~/dm-work；每一步做过就跳过
# 需要：Python 3.9+ 与 pillow numpy lz4 capstone unicorn；C 编译器（clang 或 gcc，解贴图的小段 C 用）；.NET SDK（编着色器翻译工具）；
#   spirv-cross；苹果的 Metal 着色器编译器（Mac 上随 Xcode）。缺什么本工具开头会逐项报。
# 它做的事与「./setup.sh + dm/ios/run.sh build 的生成部分」相同，只是最后不调 Xcode 构建，而是把产物放进壳里。
# Build a playable install package without Xcode: put your own game files into the "shell".
#   The shell (D2RMobile-shell.ipa) is an unsigned install package with no game content at all; this tool uses your game files to generate, on your computer, everything it lacks
#   (the rewritten game program, shader libraries, overlay, UI assets, a few data tables), puts it in, and writes out a new install package. Afterwards sign the whole package with a sideloading tool and install it.
#   The game files are only read locally and never uploaded; the resulting install package contains game content, so please use it only yourself and do not distribute it.
# Usage: d2rm_pack.py <shell.ipa> <game files directory> <output.ipa>
#   game files directory = the Update directory of Switch version 3.3.94012, containing ExeFS/{main,sdk} and RomFS/data/... (how to export it from your own game)
#   working directory (cache and intermediate outputs, about 12 GB): environment variable DM_ROOT, default ~/dm-work; every step already done is skipped
# Requires: Python 3.9+ with pillow numpy lz4 capstone unicorn; a C compiler (clang or gcc, for the small piece of C that decodes textures); the .NET SDK (to build the shader translation tool);
#   spirv-cross; Apple's Metal shader compiler (on a Mac it comes with Xcode). This tool reports whatever is missing item by item at the start.
# What it does is the same as "./setup.sh + the generation part of dm/ios/run.sh build", except that at the end it does not invoke an Xcode build but puts the build outputs into the shell.
import hashlib, json, os, re, runpy, shutil, subprocess, sys, time, zipfile

HERE = os.path.dirname(os.path.abspath(__file__)); DM = os.path.dirname(HERE); IOS = os.path.join(DM, 'ios'); GT = os.path.join(DM, 'gfx', 'tools')
ROOT = os.path.abspath(os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work'))
PY = sys.executable
WINLF = os.path.join(HERE, 'winlf'); runpy.run_path(os.path.join(WINLF, 'sitecustomize.py'))   # Windows 上统一用 \n 换行（见该目录的说明）：本进程在这里加载，子脚本经 PYTHONPATH 自动加载 | Use LF line endings on Windows (see the note in that directory): loaded here for this process, and automatically through PYTHONPATH for child scripts
MODS = ['main', 'sdk']


def say(s): print('[%s] %s' % (time.strftime('%H:%M:%S'), s), flush=True)


def die(s): sys.exit('[x] ' + s)   # 不用 ✗：中文 Windows 的控制台（GBK）打不出来，会在报错的地方再抛一个编码异常 | no ✗: the console of Chinese Windows (GBK) cannot print it, and would raise another encoding exception at the point of the error report


def run(cmd, env=None, cwd=None, log=None, what=None):
    e = dict(os.environ, DM_ROOT=ROOT, PYTHONUTF8='1', PYTHONPATH=os.pathsep.join(filter(None, [WINLF, os.environ.get('PYTHONPATH')]))); e.update(env or {})   # 子脚本一律按 UTF-8 读写与打印（Windows 缺省是本地编码，读我们的源文件、打 ✓ 都会出错） | child scripts always read, write and print in UTF-8 (the Windows default is the local encoding, and both reading our source files and printing ✓ would fail)
    r = subprocess.run(cmd, env=e, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if log: open(log, 'wb').write(r.stdout)
    if r.returncode: die('%s 没成功（退出码 %d）：%s%s' % (what or cmd[0], r.returncode, r.stdout.decode('utf-8', 'replace').strip().splitlines()[-1:] or '', '；完整输出 ' + log if log else ''))
    return r.stdout.decode('utf-8', 'replace')


def step(name, done, fn):
    if done(): say('%s：已有，跳过' % name); return
    t0 = time.time(); say(name); fn()
    if not done(): die('%s 做完了但没看到产物' % name)
    say('%s 完成（%d 秒）' % (name, time.time() - t0))


def read_shell(ipa):
    try: z = zipfile.ZipFile(ipa)
    except zipfile.BadZipFile: die('这个文件不是安装包（解不开）：%s' % ipa)
    apps = sorted({n.split('/')[1] for n in z.namelist() if n.startswith('Payload/') and n.count('/') >= 2 and n.split('/')[1].endswith('.app')})
    if len(apps) != 1: die('壳里的 App 不是恰好一个：%s' % apps)
    base = 'Payload/%s/' % apps[0]
    try:
        manifest = [l.strip() for l in z.read(base + 'dm/shell_manifest.txt').decode().splitlines() if l.strip()]
        info = dict(l.strip().split('=', 1) for l in z.read(base + 'dm/shell_info.txt').decode().splitlines() if '=' in l)
    except KeyError: die('这不是壳：里面没有 dm/shell_manifest.txt / dm/shell_info.txt（%s）' % ipa)
    exe = next((n for n in z.namelist() if n == base + apps[0][:-4]), None)
    return z, base, manifest, info, exe


def check_env(game):
    bad = []
    for m in ('PIL', 'numpy', 'lz4', 'capstone', 'unicorn'):
        try: __import__(m)
        except Exception: bad.append('Python 包 %s：%s -m pip install pillow numpy lz4 capstone unicorn' % (m, PY))
    if not (shutil.which('clang') or shutil.which('gcc') or shutil.which('cc')): bad.append('C 编译器（clang 或 gcc）')
    dotnet = shutil.which('dotnet') or (os.path.expanduser('~/.dotnet/dotnet') if os.path.exists(os.path.expanduser('~/.dotnet/dotnet')) else None)
    if not dotnet: bad.append('.NET SDK（dotnet）：https://dotnet.microsoft.com/download')
    if not shutil.which('spirv-cross'): bad.append('spirv-cross：https://github.com/KhronosGroup/SPIRV-Cross')
    for f in ('ExeFS/main', 'ExeFS/sdk', 'RomFS/data/build_manifest.psv'):
        if not os.path.isfile(os.path.join(game, f)): bad.append('游戏文件 %s（在 %s 下没找到）' % (f, game))
    if bad: die('还缺 %d 项：\n  - %s' % (len(bad), '\n  - '.join(bad)))
    return dotnet


def have_metal():   # 这台机器上有没有苹果的 Metal 着色器编译器（只有装了 Xcode 的 Mac 有） | whether this machine has Apple's Metal shader compiler (only a Mac with Xcode installed has it)
    if sys.platform != 'darwin': return False
    try: return subprocess.run(['xcrun', '-sdk', 'iphoneos', 'metal', '--version'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    except FileNotFoundError: return False


def src_digest(d):
    """翻译出的全部着色器源码的摘要（换行归一）。出壳时算一次随壳走，使用者机器上翻译完再算一次比对：
    着色器翻译工具 spirv-cross 换个版本，翻出来的源码就可能不同（实测最新源码版与 1.4.357.0 差 231 个着色器），对不上就停下来说清楚
    '''Digest of all translated shader sources (line endings normalized). Computed once when the shell is built and shipped with it, then computed again
        on the user's machine after translation and compared: a different spirv-cross version may translate shaders differently (the latest source
        and 1.4.357.0 differ in 231 shaders), so on a mismatch we stop and say why'''"""
    h = hashlib.sha256()
    for f in sorted(x for x in os.listdir(d) if x.endswith('_b.metal')):
        h.update(f.encode()); h.update(hashlib.sha256(open(os.path.join(d, f), 'rb').read().replace(b'\r\n', b'\n')).digest())
    return h.hexdigest()[:16]


def main():
    if len(sys.argv) == 3 and sys.argv[1] == '--src-digest': print(src_digest(sys.argv[2])); return   # 出壳脚本用 | used by the shell build script
    if len(sys.argv) != 4: sys.exit(__doc__ or '用法：d2rm_pack.py <壳.ipa> <游戏文件目录> <输出.ipa>')
    shell, game, out_ipa = (os.path.abspath(x) for x in sys.argv[1:])
    for st in (sys.stdout, sys.stderr):
        try: st.reconfigure(errors='replace')
        except Exception: pass
    if not os.path.isfile(shell): die('没有这个壳文件：%s' % shell)
    if not os.path.isdir(game): die('没有这个游戏文件目录：%s' % game)
    z, base, manifest, info, exe = read_shell(shell)
    P = open(os.path.join(DM, 'PRODUCT')).readline().strip()
    if info.get('PRODUCT') != P: die('壳与这份源码不是同一版：壳要的产物代号是 %s，这份源码是 %s。请用与壳同一个发布版本的源码' % (info.get('PRODUCT'), P))
    chain = info.get('CHAIN') or die('壳的版本信息里没有着色器链版本')
    dotnet = check_env(game)
    # 着色器最后一步：有苹果的编译器就在电脑上编成库；没有（Windows / Linux / 没装 Xcode 的 Mac）就把源码放进安装包，手机上用到时现编。
    # D2RM_NO_METAL=1 强制走后一种（有编译器的机器上试这条路用）
    # Last step for shaders: if Apple's compiler is present, compile them into libraries on the computer; if not (Windows / Linux / a Mac without Xcode), put the source into the install package and compile on the device when first used.
    # D2RM_NO_METAL=1 forces the latter (for trying this path on a machine that has the compiler)
    nometal = os.environ.get('D2RM_NO_METAL') == '1' or not have_metal()
    gt = (lambda script, *a: [PY, os.path.join(GT, 'nometal_run.py'), os.path.join(GT, script), *a]) if nometal else (lambda script, *a: [PY, os.path.join(GT, script), *a])
    if nometal: say('这台机器不调苹果的着色器编译器：着色器以源码放进安装包，首次启动时在手机上编（第一次进游戏会多等一会儿）')
    romfs = os.path.join(game, 'RomFS'); stage = os.path.join(ROOT, 'build', 'pack', 'stage'); shutil.rmtree(stage, ignore_errors=True); os.makedirs(os.path.join(stage, 'dm'))
    say('壳 %s：产物代号 %s，着色器链 %s，要补 %d 项' % (os.path.basename(shell), P, chain, len(manifest)))

    # 一、改写游戏程序（与 tools/product.sh build 的 touch 那一种相同）
    # 1. Rewrite the game program (same as the touch variant of tools/product.sh build)
    out = os.path.join(ROOT, 'build', 'cache', 'dm', 'out', P + '-touch'); os.makedirs(out, exist_ok=True)
    def patch():
        for m in MODS: run([PY, os.path.join(HERE, 'patch_nso.py'), os.path.join(game, 'ExeFS', m), out, m], env={'DM_RULESET': 'touch-run-v4'}, log=os.path.join(out, m + '.patch.log'), what='改写 ' + m)
    step('改写游戏程序', lambda: all(os.path.isfile(os.path.join(out, m + '.patched.bin')) and os.path.isfile(os.path.join(out, m + '.gates.tsv')) for m in MODS), patch)

    # 二、界面素材与几张数据表（与 ios/run.sh build 开头相同）
    # 2. UI assets and a few data tables (same as the beginning of ios/run.sh build)
    say('从游戏文件生成界面素材（输入没变会自己跳过）')
    run([PY, os.path.join(IOS, 'tools', 'gen_assets.py'), romfs], cwd=IOS, env={'DM_ROMFS_SRC': romfs}, log=os.path.join(stage, 'gen_assets.log'), what='生成界面素材')
    for name, gen in (('dm_vault_tab.h', 'gen_vault_table.py'), ('dm_hud_exp.h', 'gen_hud_exp.py')):
        txt = run([PY, os.path.join(IOS, 'tools', gen), romfs], what='生成 ' + name)
        with open(os.path.join(DM, 'host', name), 'w', encoding='utf-8') as f: f.write(txt)
    sys.path.insert(0, os.path.join(IOS, 'tools')); import shell_stubs
    say('数据表：经验 %d、部位 %d、仓库条目 %d、套装 %d、技能索引 %d' % shell_stubs.data(DM, os.path.join(stage, 'dm', 'derived.tsv')))
    hud = os.path.join(IOS, 'Gen', 'assets', 'NativeHUD')
    for d, src in (('hud_v4_ui', os.path.join(hud, 'v4', 'hud_v4_ui')), ('mapgoal', os.path.join(hud, 'mapgoal')), ('mercslots', os.path.join(hud, 'mercslots'))):
        if d + '/' in manifest: shutil.copytree(src, os.path.join(stage, d))
    ta = os.path.join(IOS, 'Sources', 'TouchArt')
    for f in sorted(os.listdir(ta)):
        if f in manifest: shutil.copyfile(os.path.join(ta, f), os.path.join(stage, f))

    # 三、覆盖层
    # 3. Overlay
    say('生成覆盖层')
    run([PY, os.path.join(IOS, 'tools', 'gen_romfs_overlay.py'), romfs, os.path.join(stage, 'romfs_overlay')], cwd=IOS, env={'DM_ROMFS_SRC': romfs}, log=os.path.join(stage, 'gen_overlay.log'), what='生成覆盖层')
    for d in ('romfs_overlay_astc', 'romfs_overlay_astc_variants'): shutil.rmtree(os.path.join(stage, d), ignore_errors=True)

    # 四、着色器：取包 → 翻译工具 → 翻译并编成 Metal 库（与 setup.sh 相同）
    # 4. Shaders: extract the shader packs from the game resources → translation tool → translate and compile into Metal libraries (same as setup.sh)
    packs = os.path.join(ROOT, 'build', 'cache', 'dm', 'dmsh')
    def get_packs():
        os.makedirs(packs, exist_ok=True); e = {'DMX_ROOT': os.path.join(romfs, 'data')}
        for line in run([PY, os.path.join(HERE, 'dmx.py'), 'list', 'data:data/hd/shaders/*.bin'], env=e, what='列着色器包').splitlines():
            p = line.split('\t')[0]
            if p: run([PY, os.path.join(HERE, 'dmx.py'), 'get', p, os.path.join(packs, os.path.basename(p))], env=e, what='取着色器包 ' + p)
    step('取着色器包（162 个）', lambda: os.path.isdir(packs) and len(os.listdir(packs)) == 162, get_packs)
    xl = os.path.join(ROOT, 'build', 'gfx', 'dmsh_xlate')
    step('编译着色器翻译工具', lambda: os.path.isfile(os.path.join(xl, 'dmsh_xlate')) or os.path.isfile(os.path.join(xl, 'dmsh_xlate.exe')),
         lambda: run([dotnet, 'publish', os.path.join(GT, 'dmsh_xlate'), '-c', 'Release', '-o', xl, '-v', 'quiet'], what='编译着色器翻译工具'))
    chk = lambda: subprocess.run(gt('dmsh_chain.py', '--check-tools'), env=dict(os.environ, DM_ROOT=ROOT, PYTHONUTF8='1'), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0
    if not chk():   # 本机工具与仓库登记的不同是正常的：按本机工具重新登记（会改写仓库里三个随工具变的文件，不要提交它们） | it is normal for the local tools to differ from what the repository registers: re-register by the local tools (this rewrites the three tool-dependent files in the repository; do not commit them)
        say('本机工具与仓库登记的不同：按本机工具重新登记翻译链')
        run(gt('dmsh_chain.py', '--update-lock'), what='登记翻译链'); run(gt('dmsh_chain.py', '--header'), what='写翻译链版本')
    local = run(gt('dmsh_chain.py'), what='取翻译链版本').strip().splitlines()[-1]
    ios_out = os.path.join(ROOT, 'build', 'ios', 'dmsh_b'); mac_cache = os.path.join(ROOT, 'build', 'cache', 'metal_shader_cache')
    # 做没做全只认完成标记（写在 Mac 侧缓存目录里）的逐个结果：每个着色器都是 ok，并且库目录里每个都有库文件。只看「标记在、目录里至少有一个文件」不够——
    # 翻译脚本即使全部失败也会写标记并正常退出，那样会把空的或缺了几千个的库当成做完，缺的着色器要到游戏里才报红
    # Completeness is decided only by the per-shader results in the completion marker (written in the Mac-side cache directory): every shader is ok, and every one has its library file in the library directory. "The marker exists and the directory holds at least one file" is not enough --
    # the translation script writes the marker and exits normally even when everything failed, which would accept an empty library or one missing thousands of shaders, and the missing shaders would only turn red in the game
    def shaders_done(tell=False):
        try: res = json.load(open(os.path.join(mac_cache, 'b-' + local, '_b_build.json'), encoding='utf-8'))
        except (OSError, ValueError): return False
        d = os.path.join(ios_out, 'b-' + local); have = set(os.listdir(d)) if os.path.isdir(d) else set()
        bad = [r for r in res if r['st'] != 'ok']; miss = [r['key'] for r in res if r['st'] == 'ok' and r['key'] + '_b.metallib' not in have]
        if tell and (bad or miss or not res): say('着色器 %d 个：没翻译成功 %d 个，库目录里缺 %d 个（完整输出见 %s）' % (len(res), len(bad), len(miss), os.path.join(stage, 'shaders.log')))
        return bool(res) and not bad and not miss
    step('翻译全部着色器（首次约 8 分钟）', shaders_done,
         lambda: (run(gt('dmsh_build.py', packs, mac_cache), env={'IOSOUT': ios_out, 'J': os.environ.get('J', '6'), 'MAIN': os.path.join(game, 'ExeFS', 'main')}, log=os.path.join(stage, 'shaders.log'), what='翻译着色器'), shaders_done(tell=True)))
    lib = os.path.join(stage, 'msl', 'b-' + chain); os.makedirs(os.path.dirname(lib)); shutil.copytree(os.path.join(ios_out, 'b-' + local), lib)   # 目录名用壳认的那个版本 | the directory name uses the version the shell recognizes
    if local != chain:   # 每个着色器的说明里也记着链版本，壳逐个核对（对不上拒绝加载，真机实测撞到）：一并改成壳认的那个 | each shader's description also records the chain version, and the shell checks them one by one (a mismatch is refused, hit in real-device testing): change them all to the one the shell recognizes
        old, new, n = ('"chain": "%s"' % local).encode(), ('"chain": "%s"' % chain).encode(), 0
        for f in os.listdir(lib):
            if f.endswith('.json'):
                b = open(os.path.join(lib, f), 'rb').read()
                if old in b: open(os.path.join(lib, f), 'wb').write(b.replace(old, new)); n += 1
        if not n: die('着色器说明里没找到链版本 %s（格式变了？）' % local)
    got = src_digest(os.path.join(mac_cache, 'b-' + local))
    if info.get('SHADER_SRC') and got != info['SHADER_SRC']:
        die('这台机器翻译出的着色器与壳要的不一样（摘要 %s，壳要 %s）。最常见的原因是 spirv-cross 版本不同：请换成 %s（见 docs 里的说明），'
            '然后删掉 %s 重跑。不一样的着色器装到手机上可能画错，所以不往下做' % (got, info['SHADER_SRC'], info.get('SPIRV_CROSS', '说明里写的那个版本'), os.path.join(mac_cache, 'b-' + local)))
    c1 = os.path.join(stage, 'dm', 'c1_full.tsv')
    run([PY, os.path.join(GT, 'c1_guard.py'), os.path.join(mac_cache, 'b-' + local), c1], what='出着色器下标清单')
    if local != chain:
        t = open(c1, encoding='utf-8').read(); open(c1, 'w', encoding='utf-8').write(t.replace('b-' + local, 'b-' + chain, 1))
        say('着色器库按本机工具出（链版本 %s），放进壳认的目录名 b-%s' % (local, chain))

    # 五、游戏程序写成动态库 + 装载表
    # 5. Write the game program as a dynamic library + load table
    import guest_dylib
    fw, n = guest_dylib.write(out, os.path.join(stage, 'Frameworks'), MODS); say('游戏程序动态库：%d 字节' % n)
    for f in [m + s for m in MODS for s in ('.fixups.tsv', '.gates.tsv')] + ['bootlist.tsv', 'main.cuts.tsv']:
        if os.path.isfile(os.path.join(out, f)): shutil.copyfile(os.path.join(out, f), os.path.join(stage, 'dm', f))

    # 自检：壳按链版本找着色器——目录名、下标清单的首行、每个着色器说明里的版本，三处都必须是壳认的那个（任何一处不是，壳都会拒绝加载）
    # Self-check: the shell finds shaders by chain version -- the directory name, the first line of the index list, and the version in each shader's description must all three be the one the shell recognizes (if any one is not, the shell refuses to load)
    js = [f for f in os.listdir(lib) if f.endswith('.json')]; want = ('"chain": "%s"' % chain).encode()
    wrong = [f for f in js if want not in open(os.path.join(lib, f), 'rb').read()]
    if not js or wrong: die('着色器说明里的链版本与壳要的 %s 不一致：%d / %d 个不对%s' % (chain, len(wrong), len(js), '，如 ' + wrong[0] if wrong else ''))
    if ('b-' + chain) not in open(c1, encoding='utf-8').readline(): die('着色器下标清单首行的链版本不是壳要的 %s' % chain)
    say('自检：%d 个着色器说明、下标清单、目录名的链版本都是壳要的 %s；翻译出的源码摘要 %s%s' % (len(js), chain, got, '与壳一致' if info.get('SHADER_SRC') else '（这个壳没带摘要，没比）'))

    # 六、照壳的清单核对一项不缺，再放进去
    # 6. Check against the shell's manifest that nothing is missing, then put it in
    miss = [m for m in manifest if not (os.path.isdir(os.path.join(stage, m)) if m.endswith('/') else os.path.isfile(os.path.join(stage, m)))]
    if miss: die('有 %d 项没做出来：%s。壳的清单里要、而这份工具没有生成——壳与工具不是同一版。请用与这份仓库同一版本的壳（发布页最新的 D2RMobile-shell.ipa），或把仓库更新到与壳对应的版本' % (len(miss), '、'.join(miss)))
    tmp = out_ipa + '.part'; added = 0
    with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as o:
        for i in z.infolist(): o.writestr(i, z.read(i.filename))
        for m in manifest:
            src = os.path.join(stage, m)
            files = [os.path.join(d, f) for d, _, fs in sorted(os.walk(src)) for f in sorted(fs)] if m.endswith('/') else [src]
            for f in files:
                zi = zipfile.ZipInfo(base + os.path.relpath(f, stage).replace(os.sep, '/'), date_time=(2026, 1, 1, 0, 0, 0)); zi.compress_type = zipfile.ZIP_DEFLATED
                zi.external_attr = (0o755 if os.path.basename(f) == 'Guest' else 0o644) << 16; o.writestr(zi, open(f, 'rb').read()); added += 1
    os.replace(tmp, out_ipa)
    say('好了：%s（%.0f MB，放进去 %d 个文件）。下一步：用自签工具给它签名装机（见 docs 里的说明）；这个安装包里有游戏内容，请只自己用' % (out_ipa, os.path.getsize(out_ipa) / 1048576, added))


if __name__ == '__main__': main()
