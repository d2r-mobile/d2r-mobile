#!/usr/bin/env python3
# 跨帧存游戏界面控件指针必须带界面代号（dm_wref，dm_cursor.h）：切手柄 / 触屏布局、切语言、切运行模式、面板卸载都会重建界面、旧控件释放，
# 留着的裸指针下一帧再读就是野指针（腰带补满崩溃：CRASH addr=0x28 pc=dm_actions_pump）；只比身份不解引用的，也会被新控件复用旧地址骗过。
# 静态扫 dm/host/*.c：静态存储期的变量（文件级，或函数里的 static）若是 uint64_t（标量 / 数组 / _Atomic / 结构体字段），
# 而被赋了「从界面树来的值」，就报红——这类变量一律改成 dm_wref（dm_wref_set 存、dm_wref_get 取，代号不一致取到 0）。
# 整组缓存（一个 static 结构体里存一批控件，如符文之语清单的 W）可以不逐个改：结构体里带 uint32_t ui_gen 字段、填的时候记代号，
# 且本文件里有拿它和 dm_ui_gen() 比的地方（不一致整组作废）——两样都在才放行，缺一样照样报红。
# 界面代号本身（dm_cursor.c ui_gen_scan 记上一帧的顶层面板，只比身份）登记在 EXEMPT。
# 「从界面树来的值」认这些写法：按名字 / 类型找控件的函数（ui_find vis_find ui_panel …）、子控件数组（ch + 8 * i、+ 0x58）、界面根（0x2f05458）、
# 焦点 / 悬停（+ 0x190 / + 0x178），以及走树时惯用的控件形参（w bw pw sp tw cw panel）。射程有限：经别的名字中转的值认不出，新写法要补进下面两张表。
# 用法：widget_cache_scan.py <dm/host 目录>；--selftest 先拿故意违规的文本验扫描本身会报红
# Game UI widget pointers stored across frames must carry the UI generation (dm_wref, dm_cursor.h): switching controller / touch layout, language or run mode, and panel unloads all rebuild the UI and free old widgets,
# so a raw pointer kept and read on the next frame is dangling (belt-fill crash: CRASH addr=0x28 pc=dm_actions_pump); even identity-only comparisons without dereference can be fooled by a new widget reusing the old address.
# Statically scans dm/host/*.c: a variable with static storage duration (file level, or static in a function) of type uint64_t (scalar / array / _Atomic / struct field),
# if assigned "a value from the UI tree", is reported red; such variables must all become dm_wref (store with dm_wref_set, fetch with dm_wref_get; a mismatched generation reads 0).
# Group caches (one static struct storing a batch of widgets, such as the runeword list's W) need not be converted one by one: the struct has a uint32_t ui_gen field recording the generation when filled,
# and this file compares it with dm_ui_gen() somewhere (a mismatch invalidates the whole group); both must be present to pass, and missing either is still red.
# The UI generation itself (dm_cursor.c ui_gen_scan records the previous frame's top-level panels, identity only) is registered in EXEMPT.
# "Values from the UI tree" are recognized by these patterns: functions that find widgets by name / type (ui_find vis_find ui_panel …), child widget arrays (ch + 8 * i, + 0x58), the UI root (0x2f05458),
# focus / hover (+ 0x190 / + 0x178), and the widget parameters conventionally used when walking the tree (w bw pw sp tw cw panel). Limited coverage: values relayed through other names are not recognized, and new patterns must be added to the two tables below.
# Usage: widget_cache_scan.py <dm/host directory>; --selftest first verifies that the scanner itself reports red on deliberately violating text
import os, re, sys

FIND = r'\b(?:ui_find|vis_find|find_w|ui_panel|ui_child|belt_find|belt_now|vu_find|kb_find|rw_find|find_widget|ui_top_panel)\s*\('
TREE = r'(?:\bch\s*\+\s*8\s*\*|\+\s*0x58\b|0x2f05458|\+\s*0x190\b|\+\s*0x178\b)'
PARAM = r'^(?:w|bw|pw|sp|tw|cw|vt|panel|\w+_w)$'
SRC = re.compile(FIND + '|' + TREE)

def strip(text):   # 去掉注释和字符串（保留换行，行号不变） | Strip comments and strings (keeping newlines so line numbers are unchanged)
    text = re.sub(r'/\*.*?\*/', lambda m: re.sub(r'[^\n]', ' ', m.group(0)), text, flags=re.S)
    text = re.sub(r'//[^\n]*', '', text)
    return re.sub(r'"(?:\\.|[^"\\\n])*"', '""', text)

def decl_names(decl):   # 「a, *b[4] = {0}, c = 1」→ a b c（初值里的逗号不算） | "a, *b[4] = {0}, c = 1" → a b c (commas inside initializers do not count)
    decl = re.sub(r'\{[^{}]*\}', '', decl); decl = re.sub(r'\[[^\]]*\]', '', decl)
    while re.search(r'\([^()]*\)', decl): decl = re.sub(r'\([^()]*\)', '', decl)
    out = []
    for part in decl.split(','):
        n = re.match(r'\s*\**\s*(\w+)', part)
        if n: out.append(n.group(1))
    return out

def struct_types(code):   # 字段里有 uint64_t 的 typedef 结构体类型名 | Names of typedef struct types with a uint64_t field
    return {m.group(2) for m in re.finditer(r'\btypedef\s+struct\s*\w*\s*\{([^{}]*)\}\s*(\w+)\s*;', code) if re.search(r'\buint64_t\b', m.group(1))}

def decls(code, types, static_only):   # 一段代码里声明的：uint64_t 变量名、（字段含 uint64_t 的）结构体变量名 | Declared in a code segment: uint64_t variable names, and names of struct variables (whose fields include uint64_t)
    pre = r'\bstatic\s+' if static_only else r'(?<![\w.>(,])'
    names, structs = set(), set()
    for m in re.finditer(pre + r'(?:const\s+)?(?:_Atomic\s+)?(?:volatile\s+)?uint64_t\s+([^;\n]*);', code): names.update(decl_names(m.group(1)))
    for m in re.finditer(pre + r'struct\s*\w*\s*\{([^{}]*)\}\s*([^;]*);', code):
        if re.search(r'\buint64_t\b', m.group(1)): structs.update(decl_names(m.group(2)))
    for t in types:
        for m in re.finditer(pre + re.escape(t) + r'\s+([^;(]*);', code): structs.update(decl_names(m.group(1)))
    return names, structs

def split_funcs(code):   # 顶层分块：[(起始偏移, 文本, 是函数体)]；函数体 = 顶层的 {…} | Top-level chunks: [(start offset, text, is function body)]; function body = top-level {…}
    out, depth, start, isfn = [], 0, 0, False
    for i, ch in enumerate(code):
        if ch == '{':
            if depth == 0: isfn = code[:i].rstrip().endswith(')')   # 顶层的 {…} 前面是「)」才是函数体；结构体 / 初值表留在文件级 | A top-level {…} is a function body only if preceded by ")"; structs / initializer tables stay at file level
            if depth == 0 and isfn: out.append((start, code[start:i], False)); start = i
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0 and isfn: out.append((start, code[start:i + 1], True)); start = i + 1
    out.append((start, code[start:], False)); return out

ASSIGN = re.compile(r'(?<![=!<>+\-*/%&|^\w.\]])\s*(\w+)(\s*\[[^\]=]*\])?(\s*(?:\.|->)\s*\w+(?:\s*\[[^\]=]*\])?)?\s*=(?!=)\s*([^;]+)')

EXEMPT = {('dm_cursor.c', 'ui_gen_scan'): '界面代号本身：记上一帧的顶层面板，只比身份、不解引用'}

def func_name(head):   # 函数体前面那段的最后一个「名字(」 | The last "name(" in the segment before the function body
    m = re.findall(r'(\w+)\s*\([^()]*(?:\([^()]*\)[^()]*)*\)\s*$', head)
    return m[-1] if m else ''

def group_ok(code, var):   # 整组缓存：声明里带 uint32_t ui_gen，且有「var.ui_gen 与 dm_ui_gen() 比」 | Group cache: the declaration has uint32_t ui_gen, and there is a "var.ui_gen compared with dm_ui_gen()"
    decl = re.search(r'\bstatic\s+struct\s*\w*\s*\{([^{}]*)\}\s*[^;]*\b' + re.escape(var) + r'\b[^;]*;', code)
    return bool(decl and re.search(r'\buint32_t\s+(?:\w+\s*,\s*)*ui_gen\b', decl.group(1)) and re.search(re.escape(var) + r'\s*\.\s*ui_gen\s*[!=]=\s*dm_ui_gen\s*\(\s*\)', code))

def scan(text, where):
    code = strip(text); types = struct_types(code); bad = []
    top = ''.join(t for _, t, body in split_funcs(code) if not body)   # 文件级（函数体之外） | File level (outside function bodies)
    _, fs = decls(top, types, False)   # 文件级的结构体变量（要看字段，先于去括号） | File-level struct variables (fields need checking; done before stripping braces)
    while re.search(r'\{[^{}]*\}', top): top = re.sub(r'\{[^{}]*\}', ' ', top)   # 结构体字段、初值表不是文件级变量 | Struct fields and initializer tables are not file-level variables
    fn, _ = decls(top, types, False)   # 文件级的全局 / static 都是静态存储期 | File-level globals / statics all have static storage duration
    parts = split_funcs(code)
    for k, (off, chunk, body) in enumerate(parts):
        if not body: continue
        if (where, func_name(parts[k - 1][1] if k else '')) in EXEMPT: continue
        sn, ss = decls(chunk, types, True); nost = re.sub(r'\bstatic\s+[^;]*;', ';', chunk); ln, ls = decls(nost, types, False)   # 函数里的 static；函数里的局部（遮住同名的文件级） | Statics in functions; locals in functions (shadowing file-level ones of the same name)
        ln |= set(re.findall(r'\b(?:const\s+)?(?:unsigned\s+|signed\s+)?(?:int|long|short|char|float|double|bool|BOOL|size_t|\w+_t)\b[\s*]+(\w+)\s*(?=[=;,\[)])', nost))   # 任何类型的局部与形参 | Locals and parameters of any type
        names = (fn - ln) | sn; structs = (fs - ls) | ss
        for m in ASSIGN.finditer(chunk):
            var, idx, fld, rhs = m.group(1), m.group(2), m.group(3), re.split(r'(?<![=!<>])=(?!=)', m.group(4))[-1].strip()   # 连等取最右边的值 | For chained assignments, take the rightmost value
            if not ((var in names and not fld) or (var in structs and (fld or re.match(r'^\(\s*\w+\s*\)\s*\{', rhs)))): continue
            if var in structs and group_ok(code, var): continue
            if re.match(r'(?:const\s+)?(?:_Atomic\s+)?uint64_t\b', chunk[max(0, m.start(1) - 40):m.start(1)].split(';')[-1].split('{')[-1].strip() + ' '): continue
            lhs = re.findall(r'\w+', fld)[-1] if fld else var   # 存到的名字本身就是控件（字段 w / panel、…_w）：右边不是常数就算 | The stored name is itself a widget (fields w / panel, …_w): counts whenever the right side is not a constant
            byname = re.match(r'^(?:w|panel|\w+_w)$', lhs) and re.match(r'^[A-Za-z_]\w*\s*(?:\(.*\))?\s*\)?$', rhs) and rhs.strip(') ') not in ('NULL',)   # 右边是单个名字或函数调用（几何量的算式不算） | The right side is a single name or function call (geometric arithmetic does not count)
            if byname or SRC.search(rhs) or re.match(PARAM, re.sub(r'[\s;)]+$', '', rhs)) or re.match(r'^\(\s*\w+\s*\)\s*\{\s*(?:w|bw|pw|sp|tw|cw|panel)\b', rhs):
                no = code.count('\n', 0, off + m.start(1)) + 1
                bad.append('%s:%d 静态变量 %s%s 存了控件指针，没带界面代号（改用 dm_wref）：%s' % (where, no, var, re.sub(r'\s', '', fld) if fld else '', text.split('\n')[no - 1].strip()[:140]))
        for m in re.finditer(r'atomic_(?:store|exchange)\s*\(\s*&\s*(\w+)\s*,\s*([^;]+)\)', chunk):
            var, rhs = m.group(1), m.group(2).strip()
            if var in names and (SRC.search(rhs) or re.match(PARAM, rhs)):
                no = code.count('\n', 0, off + m.start(1)) + 1
                bad.append('%s:%d 静态变量 %s 存了控件指针，没带界面代号（改用 dm_wref）：%s' % (where, no, var, text.split('\n')[no - 1].strip()[:140]))
    return bad

SELF_BAD = [
    'static uint64_t g_x_w;\nvoid f(uint64_t w) { g_x_w = w; }\n',
    'void f(uint64_t root) { static uint64_t pw; pw = ui_find(root, "X", 0); }\n',
    'static struct { uint64_t panel, row[4]; int n; } W;\nvoid f(uint64_t p) { W.panel = vis_find(p, "A", 0); }\n',
    'static struct { uint64_t panel; uint32_t ui_gen; } W;\nvoid f(uint64_t p) { W.ui_gen = dm_ui_gen(); W.panel = vis_find(p, "A", 0); }\n',
    'static _Atomic uint64_t g_t;\nvoid f(uint64_t tw) { atomic_store(&g_t, tw); }\n',
    'void f(uint64_t root) { static uint64_t c[4]; uint64_t ch = 0; c[0] = *(uint64_t *)(ch + 8 * 1); }\n',
    'static uint64_t g_d_w, g_s_w;\nvoid f(int r) { if (r == 1) g_d_w = g_s_w; }\n',
    'typedef struct { uint64_t w; int f; } ent;\nstatic ent g_e[8];\nvoid f(uint64_t w) { g_e[0] = (ent){ w, 1 }; g_e[1].w = w; }\n',
]
SELF_OK = [
    'static dm_wref g_x;\nvoid f(uint64_t w) { dm_wref_set(&g_x, w); }\n',
    'static uint64_t g_n;\nvoid f(uint64_t w) { g_n = g_n + 1; uint64_t pw = ui_find(w, "a", 0); (void)pw; }\n',
    'static uint64_t next;\nvoid f(uint64_t fr) { next = fr + 6; }\n',
    'void f(uint64_t w) { uint64_t x = w; static int k; k = 1; (void)x; }\n',
    'static uint64_t g_xy; // g_xy = w 只是注释\nvoid f(void) { g_xy = 3; }\n',
    'static uint64_t pw;\nvoid g(void) { pw = 1; }\nvoid f(uint64_t root) { uint64_t pw = ui_find(root, "a", 0); pw = ui_find(root, "b", 0); (void)pw; }\n',
    'static struct { uint64_t panel, bg; uint32_t ui_gen; } W;\nvoid f(uint64_t p) { W.ui_gen = dm_ui_gen(); W.panel = p; W.bg = find_w(p, "B", 0); }\nint ok(void) { return W.panel && W.ui_gen == dm_ui_gen(); }\n',
]

def selftest():
    ok = True
    for i, t in enumerate(SELF_BAD):
        if not scan(t, 'bad%d' % i): print('【红】自检：违规样例 %d 没报出来' % i); ok = False
    for i, t in enumerate(SELF_OK):
        r = scan(t, 'ok%d' % i)
        if r: print('【红】自检：合规样例 %d 误报：%s' % (i, r)); ok = False
    return ok

if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--selftest']
    if '--selftest' in sys.argv and not selftest(): sys.exit(1)
    d = args[0] if args else os.path.join(os.path.dirname(__file__), '..')
    bad, n = [], 0
    for f in sorted(os.listdir(d)):
        if f.endswith('.c') or f.endswith('.m'):   # 宿主自己的源文件（tests/ 下的桩不扫） | The host's own source files (stubs under tests/ are not scanned)
            n += 1; bad += scan(open(os.path.join(d, f), errors='replace').read(), f)
    for b in bad: print('【红】' + b)
    print('控件指针缓存扫描：%d 个文件，%d 处违规' % (n, len(bad)))
    sys.exit(1 if bad else 0)
