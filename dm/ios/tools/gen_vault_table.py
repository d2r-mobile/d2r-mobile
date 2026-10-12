#!/usr/bin/env python3
# 编年史仓库（第 3 步）的固定表：哪些暗金 / 套装件有格子、各在哪个部位、档次、物品图路径、套装分组与次序。
# 用法：gen_vault_table.py <原版 RomFS（Update）> [<Base RomFS>] > dm/host/dm_vault_tab.h（构建时由 dm/host/Makefile 生成，不进仓库；游戏版本换了产物有变就要复核部位件数）
# 来源全是游戏自己的表：excel/UniqueItems.txt、SetItems.txt、Sets.txt、armor / weapons / misc.txt、ItemTypes.txt；hd/items/uniques.json、sets.json、items.json（物品图 asset）。
# 收哪些：玩家可能拿到的都给一格——disabled 空、spawnable 不是 0、有底子。比原版编年史多收它不收的（disableChronicle：碎裂护身符、巨型珠宝、督军的荣耀等，
# 都能掉 / 能做出来），否则这些存不进来（页面结构原按编年史算 403 + 140）。
# 名字不在这里：宿主运行时按 index 列（字符串键）走游戏的取本地化串 main+0x7f5630。
# Fixed table for the chronicle vault (step 3): which unique / set pieces have a cell, which slot each is in, tier, item image path, set grouping and order.
# Usage: gen_vault_table.py <original game RomFS (Update)> [<Base RomFS>] > dm/host/dm_vault_tab.h (generated at build time by dm/host/Makefile, not committed to the repo; if the game version changes and the build output differs, the per-slot piece counts must be rechecked)
# Sources are all the game's own tables: excel/UniqueItems.txt, SetItems.txt, Sets.txt, armor / weapons / misc.txt, ItemTypes.txt; hd/items/uniques.json, sets.json, items.json (item image asset).
# What is included: everything a player might obtain gets a cell -- disabled empty, spawnable not 0, has a base item. It includes more than the original game's chronicle, taking what that one leaves out (disableChronicle: Sunder Charms, Colossal Jewels, Warlord's Glory, etc.,
# all of which can drop / can be crafted), otherwise these could not be stored (the page structure was originally computed from the chronicle's 403 + 140).
# Names are not here: at runtime the host goes by the index column (string key) through the game's localized-string lookup main+0x7f5630.
import json, os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'))
import dmx

PARTS = ['头盔', '盔甲', '盾牌与手套', '鞋子与腰带', '首饰', '剑与匕首', '斧与爪', '锤与棍', '长柄与矛', '弓弩', '法杖与法器']
# 部位按底子的物品类型往上找（ItemTypes Equiv1 / Equiv2）第一个命中的类型码；顺序 = 判定优先级
# 死灵头（head，原版归盾）按设计放头盔页；术士法典（grim，原版归盾）放法杖与法器页；权杖（scep，原版归法杖类 rod）放锤与棍页
# The slot is found by walking up from the base item's item type (ItemTypes Equiv1 / Equiv2) to the first matching type code; order = decision priority
# Necromancer heads (head, classed as shields in the original game) go on the helm page per the design; warlock grimoires (grim, classed as shields in the original game) go on the staves and implements page; scepters (scep, classed under the staff-like rod in the original game) go on the hammers and clubs page
PART_OF = [('helm', 0), ('head', 0), ('tors', 1), ('grim', 10), ('shld', 2), ('glov', 2), ('boot', 3), ('belt', 3),
           ('ring', 4), ('amul', 4), ('jewl', 4), ('char', 4), ('swor', 5), ('knif', 5), ('axe', 6), ('h2h', 6), ('scep', 7), ('staf', 10), ('wand', 10), ('orb', 10), ('blun', 7),
           ('pole', 8), ('spea', 8), ('jave', 8), ('miss', 9)]

def txt(name):
    raw = dmx.get('data/global/excel/%s.txt' % name).decode('utf-8', 'replace').splitlines()
    head = raw[0].split('\t'); return [dict(zip(head, l.split('\t'))) for l in raw[1:] if l.strip()]
def js(name):
    out = {}
    for d in json.loads(dmx.get('data/hd/items/%s.json' % name).decode('utf-8')):
        for k, v in d.items(): out[k] = v
    return out

def main():
    dmx.ROOT = os.path.join(sys.argv[1], 'data')
    sprites = set()
    for root in sys.argv[1:]:
        for line in open(os.path.join(root, 'data', 'build_manifest.psv'), encoding='utf-8'):
            p = line.split('|')[9].lower() if line.count('|') >= 10 else ''
            m = re.match(r'data:data/hd/global/ui/(items/.*)\.sprite$', p)
            if m: sprites.add(m.group(1))
    types = {r['Code']: (r['Equiv1'], r['Equiv2']) for r in txt('ItemTypes') if r.get('Code')}
    base = {}   # 码 → (类别目录, 类型, 档次, 物品等级) | Code → (category directory, type, tier, item level)
    for cat, f in (('armor', 'armor'), ('weapon', 'weapons'), ('misc', 'misc')):
        for r in txt(f):
            c = r.get('code', '')
            if not c: continue
            tier = 2 if c == r.get('ultracode') else 1 if c == r.get('ubercode') else 0
            base[c] = (cat, r.get('type', ''), tier, int(r.get('level') or 0))
    def part(code):
        seen, todo = set(), [base[code][1]]
        while todo:
            t = todo.pop(0)
            if not t or t in seen: continue
            seen.add(t); todo += list(types.get(t, ('', '')))
        for t, p in PART_OF:
            if t in seen: return p
        raise SystemExit('gen_vault_table：底子 %s 的类型 %s 归不进任何部位' % (code, base[code][1]))
    uj, sj, ij = js('uniques'), js('sets'), js('items')
    def icon(key, code, j):
        cat = base[code][0]
        for a in ((j.get(re.sub(r'[^a-z0-9]+', '_', key.lower().replace("'", '')).strip('_')) or {}).get('normal'), (ij.get(code) or {}).get('asset')):
            if a and 'items/%s/%s' % (cat, a.lower()) in sprites: return 'Items/%s/%s' % (cat, a)
        raise SystemExit('gen_vault_table：%s（%s）找不到物品图' % (key, code))
    ok = lambda r: r.get('spawnable') != '0' and not r.get('disabled')
    rows = []
    for r in txt('UniqueItems'):
        c = r.get('code', '')
        if not ok(r) or c not in base: continue
        rows.append((1, part(c), base[c][2], int(r['*ID']), int(r.get('lvl') or 0), r['index'], icon(r['index'], c, uj), -1))
    sets = [r['index'] for r in txt('Sets') if r.get('index')]
    for r in txt('SetItems'):
        c = r.get('item', '')
        if not ok(r) or c not in base or r['set'] not in sets: continue
        rows.append((0, part(c), base[c][2], int(r['*ID']), int(r.get('lvl') or 0), r['index'], icon(r['index'], c, sj), sets.index(r['set'])))
    # 暗金：部位 → 档次 → 物品等级 → 行号（设计「页内排序：普通 → 扩展 → 精英，同档按物品等级」）；套装：按 Sets.txt 次序，套内按 SetItems 行号
    # Uniques: slot → tier → item level → row number (design: "in-page order: normal → exceptional → elite, same tier by item level"); sets: in Sets.txt order, within a set by SetItems row number
    rows.sort(key=lambda t: (t[0], t[1], t[2], t[4], t[3]) if t[0] == 1 else (t[0], t[7], t[3]))
    used = [s for i, s in enumerate(sets) if any(t[0] == 0 and t[7] == i for t in rows)]
    q = lambda s: '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'
    print('// 生成文件，勿手改：dm/ios/tools/gen_vault_table.py <原版 RomFS>（编年史仓库固定表，见该脚本头注释）')
    print('// 部位：' + '、'.join('%d %s %d 件' % (i, p, sum(1 for t in rows if t[0] == 1 and t[1] == i)) for i, p in enumerate(PARTS)))
    print('// 暗金 %d 件、套装 %d 套 %d 件' % (sum(1 for t in rows if t[0] == 1), len(used), sum(1 for t in rows if t[0] == 0)))
    print('#define VT_NPART %d' % len(PARTS))
    print('static const char *const vt_part_name[VT_NPART] = { %s };' % ', '.join(q(p) for p in PARTS))
    print('typedef struct { uint8_t kind, part, tier; uint16_t row, lvl; int16_t set; const char *key, *icon; } vt_row;   // kind 1 暗金 / 0 套装件；row = 表行号（物品数据 +0x34）；set = 套装序号（暗金 −1）')
    print('static const vt_row vt_rows[] = {')
    for t in rows:
        set_ix = used.index(sets[t[7]]) if t[0] == 0 else -1
        print('    { %d, %d, %d, %d, %d, %d, %s, %s },' % (t[0], t[1], t[2], t[3], t[4], set_ix, q(t[5]), q(t[6])))
    print('};')
    print('#define VT_NROW (int)(sizeof vt_rows / sizeof vt_rows[0])')
    print('static const char *const vt_set_key[] = { %s };   // 套装名的字符串键（Sets.txt index），次序 = 原版' % ', '.join(q(s) for s in used))
    print('#define VT_NSET (int)(sizeof vt_set_key / sizeof vt_set_key[0])')

if __name__ == '__main__':
    main()
