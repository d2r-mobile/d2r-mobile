#!/usr/bin/env python3
# 佣兵面板加装备位用的底图（构建时从玩家的游戏文件生成到 Gen/assets/NativeHUD/mercslots，不进仓库；merc_slots.py 只拷）。用法：gen_merc_art.py <原版 RomFS>
# 原版佣兵面板底图（652×704，1 像素 = 2 布局单位）自带石纹和四个凹槽，石纹颗粒、凹槽边框都和玩家装备栏不是一套（玩家栏 = 手柄界面通用底板 MainPanelBG 第 1 帧右半的石纹
# + 透明底的格子整图 InventoryBG）。直接把角色装备栏整块扣过来。所以名字条以上整块换成玩家栏那一套，逐像素原样拷、不重画不配色：
#   石纹 = 通用底板第 1 帧里玩家栏装备区那一块（格子图在该帧里的左上角是 (814, 95)，量自实跑截图并与布局数对过）；
#   格子 = 经典版格子图 InventoryBG_Classic 的装备十格（与资料片版逐格相同，只是武器格顶上没有 I / II 页签帽——佣兵没有武器切换），药水四格那一块不贴；
#   位置 = 格子图左上角放在佣兵底图的 (DX, DY)：这样佣兵底图边框以内正好落在通用底板的石纹区里（含两个上角的角花）。merc_slots.py 按同一个偏移把十个物品格摆到玩家栏的格位。
#   面板四周 8 像素的边仍是佣兵原图的；名字条（原 y 312–422）整条下移 108；属性表五行各取一条 34 像素高的横带（原行距 47.5）依次排在名字条下面，表正好到底。
# 重新编码成原包同款 DXT5（借 gen_widestash_art.py 的解码 / 编码 / 打包）
# Base image for the extra equipment slots in the mercenary panel (generated at build time from the player's game files into Gen/assets/NativeHUD/mercslots, not in the repository; merc_slots.py only copies it). Usage: gen_merc_art.py <original RomFS>
# The original mercenary panel base image (652×704, 1 pixel = 2 layout units) comes with its own stone texture and four recesses, and neither the stone grain nor the recess borders are of the same set as the player's equipment panel (player panel = the stone texture of the right half of frame 1 of the generic controller UI base panel MainPanelBG
# + the whole grid image InventoryBG on a transparent background). The character's equipment panel block is taken over directly. So everything above the name strip is replaced with the player panel's set, copied pixel for pixel, with no redrawing and no recoloring:
#   Stone texture = the block of the player panel's equipment area in frame 1 of the generic base panel (the grid image's top-left corner in that frame is (814, 95), measured from an actual-run screenshot and checked against the layout numbers);
#   Cells = the ten equipment cells of the Classic grid image InventoryBG_Classic (cell for cell the same as the expansion version, only without the I / II tab caps on top of the weapon cells -- mercenaries have no weapon swap); the block of the four potion cells is not pasted;
#   Position = the grid image's top-left corner goes at (DX, DY) in the mercenary base image: this way the inside of the mercenary base image's border falls exactly within the stone texture area of the generic base panel (including the corner ornaments at the two top corners). merc_slots.py places the ten item cells at the player panel's cell positions with the same offset.
#   The 8-pixel border around the panel is still from the original mercenary image; the name strip (originally y 312–422) moves down as a whole by 108; each of the five stat table rows takes a 34-pixel-high horizontal band (original row pitch 47.5), placed one after another below the name strip, and the table reaches exactly to the bottom.
# Re-encoded as DXT5, the same as in the original package (borrowing the decode / encode / pack code of gen_widestash_art.py)
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image
import gen_widestash_art as GW

ART = __import__('gen_paths').gen_dir('NativeHUD', 'mercslots')
DX, DY = 63, 11                            # 格子图左上角在佣兵底图里的位置（像素） | Position (pixels) of the grid image's top-left corner in the mercenary base image
INV_AT = (814, 95)                         # 格子图左上角在通用底板第 1 帧里的位置（像素） | Position (pixels) of the grid image's top-left corner in frame 1 of the generic base panel
EQUIP_H, POTION = 392, (122, 345, 388)     # 格子图里装备区的高；药水四格所在那块（x 起、y 起、x 止，到装备区底）不贴 | Height of the equipment area in the grid image; the block holding the four potion cells (x start, y start, x end, down to the bottom of the equipment area) is not pasted
EDGE = 8                                   # 佣兵底图四周的面板边 | Panel border around the mercenary base image
BAND = (312, 422)                          # 名字条在原图里的上下沿 | Top and bottom edges of the name strip in the original image
BAND_Y, ROW_H, ROW_TOPS = 420, 34, (448, 495, 543, 590, 638)   # 名字条新上沿；属性表每行取的横带高与原图里各行横带的上沿 | New top edge of the name strip; height of the horizontal band taken for each stat table row, and the top edges of the rows' bands in the original image

def main(romfs):
    L = GW.lib(); b = GW.orig(romfs, 'hirelinginventory/v2/hirelinginventorybg'); ver, fw, w, h = struct.unpack_from('<HHII', b, 4)
    assert b[:4] == b'SpA1' and ver == 61 and (fw, w, h) == (652, 652, 704), '佣兵面板底图不是 652×704（原版资源变了？）'
    src = GW.decode(L, b); inv = GW.decode(L, GW.orig(romfs, 'inventorypanel/v2/inventorybg_classic')); assert inv.size == (507, 700), '经典版格子图不是 507×700'
    mp = GW.decode(L, GW.orig(romfs, 'mainpanelbg')); assert mp.size == (2888, 881), '通用底板不是 2888×881'
    mp = mp.crop((mp.width // 2, 0, mp.width, mp.height))   # 第 1 帧（双面板） | Frame 1 (double panel)
    im = src.copy()
    sx, sy = INV_AT[0] - DX, INV_AT[1] - DY
    im.paste(mp.crop((sx + EDGE, sy + EDGE, sx + w - EDGE, sy + BAND_Y)).convert('RGBA'), (EDGE, EDGE))   # 石纹：边框以内、名字条以上 | Stone texture: inside the border, above the name strip
    for x0, x1 in ((0, EDGE), (w - EDGE, w)): im.paste(src.crop((x0, 100, x1, 100 + BAND_Y - BAND[0])), (x0, BAND[0]))   # 左右面板边在原名字条那一段接着往下 | The left / right panel edges continue downward over the span of the original name strip
    for box in ((0, 0, inv.width, POTION[1]), (0, POTION[1], POTION[0], EQUIP_H), (POTION[2], POTION[1], inv.width, EQUIP_H)):
        fr = inv.crop(box); im.paste(fr, (DX + box[0], DY + box[1]), fr)
    im.paste(src.crop((0, BAND[0], w, BAND[1])), (0, BAND_Y)); y = BAND_Y + BAND[1] - BAND[0]
    for t in ROW_TOPS: im.paste(src.crop((0, t, w, t + ROW_H)), (0, y)); y += ROW_H
    if y < h: im.paste(src.crop((0, h - (h - y), w, h)), (0, y))   # 余下的贴原图最底下那几行（面板下边） | The rest is filled with the bottom-most rows of the original image (the panel's bottom edge)
    o = GW.pack(L, b, GW.bc3(im), w, h, w); open(os.path.join(ART, 'HirelingInventoryBG.sprite'), 'wb').write(o); im.save(os.path.join(ART, 'HirelingInventoryBG.png'))
    print('HirelingInventoryBG：%d×%d，%d 字节' % (w, h, len(o)))

if __name__ == '__main__': main(sys.argv[1])
