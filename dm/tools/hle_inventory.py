# HLE 审计清单：抽 dm/host 下全部 HLE_FORCE / HLE 条目与 libc 直通表、svc 分派，输出 TSV（类别 文件 行 改编名 C 名 理由 函数体行数 体内特征）
# 体内特征只是粗筛提示（如「只 return RES_OK」），三类判定（完整 / 简化 / 剪口）须逐条读体后人工定。
# 用法：hle_inventory.py [输出 tsv]
# HLE audit list: extracts all HLE_FORCE / HLE entries under dm/host plus the libc passthrough table and svc dispatch, and outputs TSV (category, file, line, mangled name, C name, reason, function body line count, body features)
# Body features are only a coarse screening hint (e.g. "only return RES_OK"); the three-way classification (full / simplified / cut) must be decided manually after reading each body.
# Usage: hle_inventory.py [output tsv]
import re, sys, glob, os
H = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'host')
rows = []
def body_of(src, start):
    i = src.index('{', start); d = 0
    for j in range(i, len(src)):
        if src[j] == '{': d += 1
        elif src[j] == '}':
            d -= 1
            if not d: return src[i:j + 1]
    return ''
def feat(b):
    s = re.sub(r'\s+', ' ', b)
    f = []
    if re.fullmatch(r'\{ ?(\(void\)[^;]*; ?)*return (RES_OK|0|RES_FAIL|0x[0-9a-fA-F]+u?|1|true|false); ?\}', s): f.append('只返回常量')
    if 'RES_FAIL' in s: f.append('含失败返回')
    if re.search(r'LOG\(|die\(', s): f.append('有日志/中止')
    if len(s) < 60: f.append('极短')
    return ','.join(f)
for fn in sorted(glob.glob(os.path.join(H, 'hle_*.c'))):
    src = open(fn, encoding='utf-8').read()
    for m in re.finditer(r'\bHLE_FORCE\(\s*"([^"]+)"\s*,\s*"((?:[^"\\]|\\.)*)"\s*,\s*([^,]+),\s*(\w+)', src):
        b = body_of(src, m.end()); rows.append(('FORCE', os.path.basename(fn), src[:m.start()].count('\n') + 1, m.group(1), m.group(4), m.group(2), b.count('\n') + 1, feat(b)))
    for m in re.finditer(r'(?<![_A-Z])HLE\(\s*"([^"]+)"\s*,\s*([^,]+),\s*(\w+)', src):
        b = body_of(src, m.end()); rows.append(('HLE', os.path.basename(fn), src[:m.start()].count('\n') + 1, m.group(1), m.group(3), '', b.count('\n') + 1, feat(b)))
src = open(os.path.join(H, 'hle_table.c'), encoding='utf-8').read()
for n in re.findall(r'\bL\((\w+)\)', src): rows.append(('LIBC', 'hle_table.c', 0, n, n, 'libc 直通', 0, ''))
out = sys.argv[1] if len(sys.argv) > 1 else '/dev/stdout'
with open(out, 'w', encoding='utf-8') as f:
    f.write('类别\t文件\t行\t改编名\tC名\t理由\t体行数\t特征\n')
    for r in rows: f.write('\t'.join(map(str, r)) + '\n')
from collections import Counter
print('条目', len(rows), Counter(r[0] for r in rows), file=sys.stderr)
print('只返回常量 / 极短：', sum(1 for r in rows if '只返回常量' in r[7]), file=sys.stderr)
