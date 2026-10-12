"""界面布局 JSON 的结构补丁：仓库里只放「我们在原版布局上改了什么」，构建时套到玩家游戏文件里的原版布局上（不放原版布局本身）。
补丁格式（递归）：
  {"value": X}                              整个换成 X（类型变了、或叶子值变了）
  {"dict": {键: 子补丁 | null}, "order": [键…]}  字典：子补丁为 null = 原样；不在原版里的键给 {"value": …}；order = 结果的键顺序（删掉的键不在里面）
  {"list": [{"base": i, "patch": 子补丁 | null} | {"value": X}, …]}   列表：按结果的顺序，逐项指明取原版第 i 项（再套子补丁）或新值
列表项按控件名（"name"）对应原版，名字对不上再按位置。用法：
  layout_patch.py diff <原版 json> <我们的 json> > 补丁.json    （开发期：改了布局后重出补丁）
  apply(原版对象, 补丁) → 我们的对象
  Structural patch for UI layout JSON: the repo holds only "what we changed on top of the original game's layout", applied at build time onto the original layout in the player's game files (the original layout itself is not stored).
  Patch format (recursive):
    {"value": X}                              replace the whole thing with X (type changed, or leaf value changed)
    {"dict": {key: sub-patch | null}, "order": [key...]}  dict: sub-patch null = unchanged; keys not in the original are given as {"value": ...}; order = key order of the result (deleted keys are not in it)
    {"list": [{"base": i, "patch": sub-patch | null} | {"value": X}, ...]}   list: in result order, each item states whether to take the original's item i (then apply the sub-patch) or a new value
  List items are matched to the original by widget name ("name"), falling back to position when names do not match. Usage:
    layout_patch.py diff <original json> <our json> > patch.json    (during development: regenerate the patch after changing the layout)
    apply(original object, patch) → our object"""
import json, re, sys


def load_text(t):
    t = re.sub(r'//.*', '', t); t = re.sub(r',(\s*[}\]])', r'\1', t); return json.loads(t)


def diff(a, b):
    if type(a) != type(b): return {'value': b}
    if isinstance(a, dict):
        sub = {k: (diff(a[k], b[k]) if k in a else {'value': b[k]}) for k in b}
        if all(v is None for v in sub.values()) and list(a) == list(b): return None
        return {'dict': {k: v for k, v in sub.items() if v is not None}, 'order': list(b)}
    if isinstance(a, list):
        names = {x.get('name'): i for i, x in enumerate(a) if isinstance(x, dict) and x.get('name')}
        spec, used, changed = [], set(), len(a) != len(b)
        for j, y in enumerate(b):
            i = names.get(y.get('name')) if isinstance(y, dict) and y.get('name') else None
            if i is None and j < len(a) and j not in used and not (isinstance(a[j], dict) and a[j].get('name')): i = j
            if i is None or i in used: spec.append({'value': y}); changed = True; continue
            used.add(i); p = diff(a[i], y); spec.append({'base': i, 'patch': p}); changed |= p is not None or i != j
        return {'list': spec} if changed else None
    return None if a == b else {'value': b}


def apply(a, p):
    if p is None: return a
    if 'value' in p: return p['value']
    if 'dict' in p: return {k: apply(a.get(k), p['dict'].get(k)) if k in a else p['dict'][k]['value'] for k in p['order']}
    if 'list' in p: return [apply(a[s['base']], s['patch']) if 'base' in s else s['value'] for s in p['list']]
    raise ValueError('layout_patch：不认识的补丁 %r' % list(p))


if __name__ == '__main__' and sys.argv[1:2] == ['diff']:
    a = load_text(open(sys.argv[2], encoding='utf-8-sig').read()); b = load_text(open(sys.argv[3], encoding='utf-8-sig').read())
    p = diff(a, b); assert apply(a, p) == b and json.dumps(apply(a, p)) == json.dumps(b)
    json.dump(p, sys.stdout, ensure_ascii=False, indent=1); sys.stdout.write('\n')
