#!/usr/bin/env python3
# 宿主经验表 dm_hud_exp.h（底栏经验条用）：由原版 data/global/excel/experience.txt 生成（各职业同值，取 Amazon 列）。构建时生成，不进仓库。
# 用法：gen_hud_exp.py <原版 RomFS（Update）> > <输出 .h>
# Host experience table dm_hud_exp.h (for the bottom bar experience bar): generated from the original game's data/global/excel/experience.txt (same values for every class; the Amazon column is used). Generated at build time, not committed to the repository.
# Usage: gen_hud_exp.py <original game RomFS (Update)> > <output .h>
import csv, io, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import romfs_art

def main(romfs):
    rows = list(csv.DictReader(io.StringIO(romfs_art.read(romfs, 'global/excel/experience.txt').decode('utf-8')), delimiter='\t'))
    v = [int(r['Amazon']) for r in rows if r.get('Level', '').isdigit()][:100]
    if len(v) != 100: raise SystemExit('gen_hud_exp：experience.txt 只有 %d 级' % len(v))
    out = ['// 由原版 data/global/excel/experience.txt 生成（3.3.94012，各职业同值，取 Amazon 列）：k_exp[L] = 升到 L+1 级所需总经验；L = 0..99',
           '#pragma once', '#include <stdint.h>', 'static const uint32_t k_exp[100] = {']
    for i in range(0, 100, 10): out.append('    ' + ', '.join(str(x) for x in v[i:i + 10]) + (',' if i < 90 else ''))
    out.append('};')
    sys.stdout.write('\n'.join(out) + '\n')

if __name__ == '__main__':
    main(sys.argv[1])
