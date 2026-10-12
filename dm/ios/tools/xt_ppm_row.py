#!/usr/bin/env python3
# 读 xtrace_report.py 的输出，按首绘片元键归并各趟 GPU 区间（片元 + 顶点），除以深度预通道片元（94949567 @ 2151x990）做同局归一，打一行
# Read the output of xtrace_report.py, merge each pass's GPU intervals (fragment + vertex) by first-draw fragment key, divide by the depth pre-pass fragment (94949567 @ 2151x990) to normalize within the same run, and print one line
import sys, re, collections
name, red = sys.argv[1], sys.argv[2]
g = collections.Counter(); tot = nf = ref = 0
for l in sys.stdin:
    m = re.search(r'(\d+) 帧.*合计 ([\d.]+)', l)
    if m and 'metal-gpu-intervals' in l: nf = int(m[1]); tot = float(m[2])
    m = re.match(r'\s+([\d.]+) ms/帧\s+[\d.]+%\s+[\d.]+ 段/帧\s+(\w+)\s+p\d+ (\d+x\d+) (\w+) d\d+', l)
    if not m: continue
    g[m[4]] += float(m[1])
    if m[4] == '94949567' and m[3] == '2151x990' and m[2] == 'Fragment': ref = float(m[1])
keys = [('5c1ca4e7', '场景'), ('a1b1aab6', '色调映射趟'), ('2e2d6700', '泛光叠加'), ('f371fb3f', '大气'), ('9a650561', '合成趟'), ('52aac1f2', '放大'),
        ('ebcfcd1f', 'FXAA'), ('d5fcb22f', '颜色拷贝'), ('45a5b1ff', '颜色降采样'), ('56c2fef6', '线性化深度'), ('fbb33036', '透明合并'), ('8e80084d', '透明上采样'), ('2938a5b7', '泛光截取')]
ref = ref or 1e9
warn = '' if nf >= 350 and 5 <= tot <= 13 else '  ⚠作废（帧数 / 采样量异常：正常约 440 帧、合计 7–10）'
print('%s\t%d 帧 合计 %.2f 红 %s\t' % (name, nf, tot, red) + '  '.join('%s %.2f' % (n, g[k] / ref) for k, n in keys if g[k] > 0) + '  | 合计÷基准 %.1f' % (tot / ref) + warn)
