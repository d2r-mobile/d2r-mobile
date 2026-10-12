#!/usr/bin/env python3
# 百宝囊框贴图（背包下方金币框左边一个同款框）：从原版物品栏底图 InventoryBG_Classic（网格 + 金币框，507×700）
# 裁金币框那一块（像素 x 152–352、y 628–692），左右各留 24 像素的框边、中间压窄到 140 像素宽，存成同格式精灵图 Gen/assets/NativeHUD/pouch/PouchBox.sprite（构建时生成，不进仓库）。
# 底图里金币框外圈在 x 154–351、y 627–691（逐行量透明度）。用法：gen_pouch_art.py <原版 RomFS（Update）>（构建时跑；pouch_layout.py 只拷）
# Pouch box texture (a matching box to the left of the gold box below the inventory): from the original game's inventory base image InventoryBG_Classic (grid + gold box, 507×700)
# crop the gold box piece (pixels x 152–352, y 628–692), keep 24 pixels of box border on each side and squeeze the middle to 140 pixels wide, saved as a sprite of the same format Gen/assets/NativeHUD/pouch/PouchBox.sprite (generated at build time, not committed to the repo).
# In the base image the gold box's outer ring is at x 154–351, y 627–691 (alpha measured row by row). Usage: gen_pouch_art.py <original game RomFS (Update)> (run at build time; pouch_layout.py only copies)
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE); sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools'))
import gen_widestash_art as G, dmx
from PIL import Image

W, CAP = 140, 24

def main():
    dmx.ROOT = os.path.join(sys.argv[1], 'data'); L = G.lib()
    src = dmx.get('data/hd/global/ui/controller/panel/inventorypanel/v2/inventorybg_classic.sprite'); im = G.decode(L, src)
    fr = im.crop((152, 628, 352, 692)); h = fr.height
    out = Image.new('RGBA', (W, h)); out.paste(fr.crop((0, 0, CAP, h)), (0, 0)); out.paste(fr.crop((fr.width - CAP, 0, fr.width, h)), (W - CAP, 0))
    out.paste(fr.crop((CAP, 0, fr.width - CAP, h)).resize((W - 2 * CAP, h), Image.LANCZOS), (CAP, 0))
    p = os.path.join(__import__('gen_paths').gen_dir('NativeHUD', 'pouch'), 'PouchBox.sprite'); open(p, 'wb').write(G.pack(L, src, G.bc3(out), W, h, W))
    print('PouchBox：%d×%d 像素（布局 %d×%d 单位）→ %s' % (W, h, 2 * W, 2 * h, p))

if __name__ == '__main__':
    main()
