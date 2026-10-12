# UI 消息流对照（格式与规则见 dm/verify/ui_message_stream.md）：两份 uimsg.jsonl，按 (cat_h, act_h, arg) 比，相邻相同折叠。
# 用法：uicmp.py <真值 uimsg.jsonl> <宿主 uimsg.jsonl> [--until <arg>]   退出码 0 = 一致
#   --until：两边都截到第一次出现该载荷（含）为止，比同一段流程（如不按键只到选角主菜单 MainMenuButtonRibbon）
# UI message stream comparison (format and rules in dm/verify/ui_message_stream.md): two uimsg.jsonl files compared by (cat_h, act_h, arg), with adjacent duplicates collapsed.
# Usage: uicmp.py <ground truth uimsg.jsonl> <host uimsg.jsonl> [--until <arg>]   exit code 0 = match
#   --until: both sides are cut at the first occurrence of that payload (inclusive), comparing the same stretch of flow (e.g. with no key presses only up to the character select main menu MainMenuButtonRibbon)
import sys, json
UNTIL = sys.argv[sys.argv.index('--until') + 1] if '--until' in sys.argv else None
# 登记在案的豁免（格式与证据见 ui_message_stream.md「豁免表」）：(动作名, 载荷)
# Registered exemptions (format and evidence in the 「豁免表」(exemption table) section of ui_message_stream.md): (action name, payload)
EXEMPT = {('OpenPanel', 'SaveIndicatorPanel'), ('ClosePanel', 'SaveIndicatorPanel')}
EXEMPT_H = {('0x723d40e52f74e229', 'SaveIndicatorPanel')}   # OpenPanel 的哈希；ClosePanel 按名字核 | Hash of OpenPanel; ClosePanel is checked by name
def exempt(r): return (r.get('act'), r.get('arg')) in EXEMPT or (r['act_h'], r.get('arg')) in EXEMPT_H
def load(p):
    raw = [json.loads(l) for l in open(p, encoding='utf-8') if l.strip()]
    seq = [r for r in raw if not exempt(r)]
    load.dropped = getattr(load, 'dropped', 0) + len(raw) - len(seq)
    if UNTIL:
        k = next((i for i, r in enumerate(seq) if r.get('arg') == UNTIL), None)
        if k is None: print(f'{p} 里没有 {UNTIL}'); sys.exit(2)
        seq = seq[:k + 1]
    keys = [(r['cat_h'], r['act_h'], r.get('arg')) for r in seq]
    names = {(r['cat_h'], r['act_h'], r.get('arg')): f"{r.get('cat', r['cat_h'])}:{r.get('act', r['act_h'])}:{r.get('arg')}" for r in seq}
    fold = [k for i, k in enumerate(keys) if i == 0 or k != keys[i - 1]]
    return keys, fold, names
a, fa, na = load(sys.argv[1]); b, fb, nb = load(sys.argv[2]); names = {**nb, **na}
n = next((i for i, (x, y) in enumerate(zip(fa, fb)) if x != y), None)
if n is None and len(fa) != len(fb): n = min(len(fa), len(fb))
print(f'射程：真值 {len(a)} 条（折叠后 {len(fa)}），宿主 {len(b)} 条（折叠后 {len(fb)}），类别 {len({k[0] for k in fa + fb})} 种；豁免剔除 {load.dropped} 条')
if n is None: print('UICMP 一致'); sys.exit(0)
print(f'UICMP 不一致：折叠后第 {n} 条起')
for lab, f in (('真值', fa), ('宿主', fb)):
    print(f'  {lab}:'); [print(f"   {'>>' if i == n else '  '} {i} {names.get(f[i], f[i])}") for i in range(max(0, n - 3), min(len(f), n + 4))]
sys.exit(1)
