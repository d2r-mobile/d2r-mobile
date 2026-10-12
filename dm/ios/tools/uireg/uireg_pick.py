#!/usr/bin/env python3
# 按改动挑局：git diff 基点..被测头 的文件 → affected.tsv → 受影响局；开跑前查基点。
# 合入门槛从简 = Mac 冒烟 + 一档受影响局；没有受影响局时跑核心局；--core 时再并上核心局。整表只放夜里。
# 用法：uireg_pick.py <被测提交或分支> [--main main]
# 输出最后一行「局：…」给脚本用（含 * 表示要整表）；退出码 0 = 可以跑，3 = 基点旧且主干改动碰到受影响局（先变基，不跑注定失败的局）。
# Pick runs by change: files from git diff base..head under test → affected.tsv → affected runs; check the base before starting.
# The merge threshold kept simple = Mac smoke test + affected runs in one mode; when there are no affected runs, run the core runs; with --core the core runs are added on top. The full table runs only at night.
# Usage: uireg_pick.py <commit or branch under test> [--main main]
# The last output line 「局：…」(runs: …) is for scripts (containing * means the full table is needed); exit code 0 = OK to run, 3 = the base is old and mainline changes touch affected runs (rebase first, do not run runs that are bound to fail).
import sys, os, re, fnmatch, subprocess
CORE = "e k s sb t pk rf2 pg2 vt1 j".split()   # 核心局：前端 / 镶嵌与符文之语 / 储藏箱存取与按钮 / 魔盒 / iPad 身体框 / 重开 / 个人仓库翻页 / 编年史仓库；单档运行合计约 6.5 分钟 | core runs: front end / socketing and runewords / stash deposit-withdraw and buttons / cube / iPad body box / restart / personal stash paging / chronicle vault; about 6.5 minutes in total for a single-mode pass
H = os.path.dirname(os.path.abspath(__file__))
def git(*a): return subprocess.run(["git", *a], capture_output=True, text=True, check=True).stdout
def rules():
    r = []
    for ln in open(os.path.join(H, "affected.tsv"), encoding="utf-8"):
        if not ln.strip() or ln.startswith("#"): continue
        pat, games = ln.rstrip("\n").split("\t", 1); r.append((pat, games.split()))
    return r
def uireg_games(base, head):   # uireg.py 改动落在哪些局：改动行号 → 往上找最近的 G("名" | which runs a uireg.py change falls into: changed line number → search upward for the nearest G("name"
    out = git("diff", "-U0", base, head, "--", "dm/ios/tools/uireg/uireg.py")
    if not out: return set()
    src = git("show", f"{head}:dm/ios/tools/uireg/uireg.py").split("\n"); gs = set()
    for m in re.finditer(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", out, re.M):
        a, n = int(m.group(1)), int(m.group(2) or 1)
        for ln in range(a, a + max(n, 1)):
            for j in range(min(ln, len(src)) - 1, -1, -1):
                g = re.search(r'\bG\("([a-z0-9]+)"', src[j])
                if g: gs.add(g.group(1)); break
    return gs
def classify(files, base, head, R):
    pick, why, unreg = set(), [], []
    for f in files:
        hit = next(((p, g) for p, g in R if fnmatch.fnmatch(f, p)), None)
        if hit is None:
            if f.startswith("dm/"): unreg.append(f); pick.add("*"); why.append(f"{f} → 未登记，按整表")
            continue
        p, g = hit
        if g == ["-"]: why.append(f"{f} → 只跑核心局（{p}）"); continue
        if g == ["@uireg"]: u = uireg_games(base, head); pick |= u; why.append(f"{f} → 改了这些局的脚本：{' '.join(sorted(u)) or '无'}"); continue
        pick |= set(g); why.append(f"{f} → {' '.join(g)}（{p}）")
    return pick, why, unreg
def main():
    a = [x for x in sys.argv[1:] if not x.startswith("--")]; head = a[0] if a else "HEAD"
    mainref = sys.argv[sys.argv.index("--main") + 1] if "--main" in sys.argv else "main"
    R = rules(); mb = git("merge-base", head, mainref).strip()
    files = [f for f in git("diff", "--name-only", mb, head).split("\n") if f]
    pick, why, unreg = classify(files, mb, head, R)
    ahead = int(git("rev-list", "--count", f"{mb}..{mainref}").strip() or 0)
    print(f"被测 {git('rev-parse', '--short', head).strip()}，基点 {mb[:9]}，主干 {mainref} 领先基点 {ahead} 个提交；改动文件 {len(files)} 个")
    for w in why: print("  " + w)
    rc = 0
    if ahead:
        mfiles = [f for f in git("diff", "--name-only", mb, mainref).split("\n") if f]
        mp, mwhy, _ = classify(mfiles, mb, mainref, R)
        mp.discard("@uireg")
        sel = pick | set(CORE)
        clash = "*" in mp or ("*" in pick and mp) or (mp & sel)
        if clash:
            rc = 3; print(f"【红】基点旧：主干在基点之后改了受影响局用到的东西（{' '.join(sorted(mp & sel)) if '*' not in mp else '整表级改动'}），先变基到 {git('rev-parse', '--short', mainref).strip()} 再跑：")
            for w in mwhy:
                if not w.endswith("只跑核心局（-）") and "→ 只跑核心局" not in w: print("    主干 " + w)
        else: print("基点虽旧，主干的改动不碰这次要跑的局，可以跑")
    full = "*" in pick
    aff = sorted(pick - {"*"}); core = "--core" in sys.argv or not aff
    games = ["*"] if full else aff + ([c for c in CORE if c not in pick] if core else [])
    print(f"受影响局：{'整表' if full else ' '.join(sorted(pick)) or '无'}" + (f"；并上核心局：{' '.join(CORE)}" if not full and core else "") + (f"；未登记文件 {len(unreg)} 个（请补进 affected.tsv）" if unreg else ""))
    print("局：" + " ".join(games)); sys.exit(rc)
if __name__ == "__main__": main()
