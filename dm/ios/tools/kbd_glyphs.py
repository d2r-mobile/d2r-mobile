# Mac App 的键盘键帽：玩家用键盘时，按键提示条 / 技能格 / 页签两端画的手柄键图标换成对应的键盘键名（宿主 dm_padmode.c 把取图路径改到这一套）。
# 22 张原版键帽（Controller/ControllerButtons/switch/，48×45）里有键盘对应键的那些各出一张同尺寸的：底板取原版肩键那块石板的配色（左缘一列 + 板面一列，
# 竖向拉到键盘键的高），键名用原版界面字体白字黑边；没有键盘对应键的（右摇杆、十字键、刷新小图标）原图不动。键位同 host/front_mac.m key_bit。
# Keyboard key caps of the Mac App: when the player uses the keyboard, the gamepad button icons drawn on the key-hint bar / skill slots / ends of tab rows are replaced by the corresponding keyboard key names (the host's dm_padmode.c switches the image path to this set).
# Of the 22 original key caps (Controller/ControllerButtons/switch/, 48x45), each one with a keyboard counterpart gets an image of the same size: the plate takes the colours of the stone slab of the original shoulder button (one column from the left edge + one column of the face,
# stretched vertically to the height of a keyboard key), and the key name is in the original UI font, white with a black outline; those without a keyboard counterpart (right stick, D-pad, the small refresh icon) keep the original image. Key mapping as in host/front_mac.m key_bit.
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from PIL import Image, ImageDraw, ImageFont
import romfs_art, gen_widestash_art as GW

DIR = 'data/hd/global/ui/Controller/ControllerButtons/switch/'
KEYS = {'ButtonRight': 'J', 'ButtonBottom': 'K', 'ButtonTop': 'U', 'ButtonLeft': 'I', 'BumperLeft': 'Q', 'BumperLeftRectangle': 'Q', 'BumperRight': 'E', 'BumperRightRectangle': 'E',
        'TriggerLeft': 'Z', 'TriggerRight': 'C', 'ButtonStart': '=', 'ButtonSelect': 'Tab', 'StickLeftButton': 'F', 'StickRightButton': 'G', 'StickLeft': 'WASD'}
ALL = 22
S = 4   # 先按 4 倍画再缩回去：小图上的圆角与字边才平滑 | draw at 4x and scale back down: rounded corners and glyph edges are only smooth that way on a small image

def _cap(plate, label, font):
    """plate = 原版肩键图（取配色）；返回 48×45 的键盘键帽。 | plate = the original shoulder-button image (for its colours); returns a 48x45 keyboard key cap."""
    w = 30 if len(label) == 1 else 44; x0 = (45 - w) // 2; y0, y1 = 9, 35; n = y1 - y0
    edge = [plate.getpixel((11, 16 + round(i * 13 / (n - 1)))) for i in range(n)]; face = [plate.getpixel((14, 16 + round(i * 13 / (n - 1)))) for i in range(n)]
    im = Image.new('RGBA', (48, 45), (0, 0, 0, 0))
    for i in range(n):
        for x in range(x0, x0 + w): im.putpixel((x, y0 + i), edge[i] if x in (x0, x0 + w - 1) else face[i])
    for x, y in ((x0, y0), (x0 + w - 1, y0), (x0, y1 - 1), (x0 + w - 1, y1 - 1)): im.putpixel((x, y), (0, 0, 0, 0))   # 四角各去一个像素 | remove one pixel at each of the four corners
    big = Image.new('RGBA', (48 * S, 45 * S), (0, 0, 0, 0)); g = ImageDraw.Draw(big)
    f = ImageFont.truetype(font, (19 if len(label) == 1 else 13 if len(label) == 3 else 10) * S); bb = g.textbbox((0, 0), label, font=f, stroke_width=S)
    g.text(((x0 + w / 2) * S - (bb[0] + bb[2]) / 2, (y0 + n / 2 - .5) * S - (bb[1] + bb[3]) / 2), label, font=f, fill=(255, 255, 255, 255), stroke_width=S, stroke_fill=(16, 16, 18, 255))
    im.alpha_composite(big.resize((48, 45), Image.LANCZOS))
    return im

def build(romfs, outdir):
    """返回 {原版键帽逻辑路径（小写）: 生成的贴图文件}，22 张齐全（没有键盘对应键的是原图字节）。 | Returns {logical path of the original key cap (lower case): generated texture file}, all 22 present (those with no keyboard counterpart are the original image bytes)."""
    L = GW.lib(); font = romfs_art.font(romfs, 'BlizzardGlobal-V5_81.ttf'); plate = romfs_art.sprite(romfs, DIR + 'BumperLeft.sprite'); repl = {}
    for line in open(os.path.join(romfs, 'data', 'build_manifest.psv'), encoding='utf-8'):
        f = line.rstrip('\n').split('|')
        if len(f) < 11 or line.startswith('#') or '/ui/controller/controllerbuttons/switch/' not in f[9].lower(): continue
        path = f[9].split(':', 1)[1]; name = os.path.basename(path)[:-len('.sprite')]; b = romfs_art.read(romfs, path); o = os.path.join(outdir, 'kbd_' + name + '.sprite')
        if name in KEYS:
            ver, fw, w, h = struct.unpack_from('<HHII', b, 4); assert b[:4] == b'SpA1' and ver == 61 and (w, h) == (48, 45), path
            b = GW.pack(L, b, GW.bc3(_cap(plate, KEYS[name], font)), w, h, fw)
        open(o, 'wb').write(b); repl[path.lower()] = o
    if len(repl) != ALL or sum(1 for k in repl if os.path.basename(k)[:-7] in {n.lower() for n in KEYS}) != len(KEYS): raise SystemExit('kbd_glyphs：手柄键图标应为 %d 张、其中 %d 张有键盘对应键，实际 %d 张（原包变了？）' % (ALL, len(KEYS), len(repl)))
    return repl

if __name__ == '__main__':   # 预览：kbd_glyphs.py <资源目录> <输出目录>，另存一张放大的总览图 | preview: kbd_glyphs.py <resource directory> <output directory>, which also saves an enlarged overview image
    r = build(sys.argv[1], sys.argv[2]); L = GW.lib(); z = 6; sheet = Image.new('RGBA', (8 * 48 * z, 3 * 45 * z), (40, 40, 40, 255))
    for i, k in enumerate(sorted(r)): im = GW.decode(L, open(r[k], 'rb').read()); sheet.alpha_composite(im.resize((im.width * z, im.height * z), Image.NEAREST), ((i % 8) * 48 * z, (i // 8) * 45 * z))
    sheet.save(os.path.join(sys.argv[2], 'kbd_sheet.png')); print(len(r))
