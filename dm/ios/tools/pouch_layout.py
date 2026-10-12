#!/usr/bin/env python3
# 百宝囊框与「存放」按钮（宿主 dm/host/dm_pouch.c）：物品栏面板（手柄 Switch 版布局）底部金币框左右各加一个我们自己的控件。
#   左：百宝囊框 = 原版金币框那张底（gen_pouch_art.py 从物品栏底图裁、压窄）+ 原版符文物品图 + 件数；点它宿主出清单（原版说明框样式）。
#   右：「存放」= 原版前端小按钮图 FrontEnd_ButtonSmall + 原版字符串 @strGoldDeposit（存放）。
# 两个按钮都发 PanelManager:ClosePanel:TouchPouch / TouchStore（空操作，宿主当信号收，同编年史页子页签）。全包在一个容器 TouchPouchBox 里（面板直接子控件数要 ≤ 256，见 touch_layout._RW）。
# 坐标：物品栏底图摆在面板 (38 + …) 处、1 像素 = 2 单位，金币框外圈在单位 x 512–906、y 1294–1422；网格 x 216–1196。框与按钮各占金币框两侧的空当。
# 宿主按「自定义」页开关把整个容器挪到屏外（关）或挪回（开）
# Pouch box and 「存放」(Deposit) button (host dm/host/dm_pouch.c): one widget of our own is added on each side of the gold box at the bottom of the inventory panel (controller Switch layout).
#   Left: pouch box = the original game's gold box backing (cropped from the inventory base image and narrowed by gen_pouch_art.py) + the original rune item image + piece count; tapping it makes the host show a list (in the original game's tooltip box style).
#   Right: 「存放」(Deposit) = the original front-end small button image FrontEnd_ButtonSmall + the original string @strGoldDeposit (Deposit).
# Both buttons send PanelManager:ClosePanel:TouchPouch / TouchStore (a no-op; the host receives it as a signal, same as the chronicle page's sub-tabs). Everything is wrapped in one container TouchPouchBox (the panel's direct child widget count must be ≤ 256, see touch_layout._RW).
# Coordinates: the inventory base image is placed at (38 + ...) in the panel, 1 pixel = 2 units, the gold box's outer ring is at units x 512–906, y 1294–1422; grid x 216–1196. The box and the button each occupy the gap on either side of the gold box.
# The host moves the whole container off screen (off) or back (on) according to the switch on the 「自定义」(Custom) page
import os, shutil
import touch_layout

HERE = os.path.dirname(os.path.abspath(__file__))
INV = 'controller/console/switch/playerinventoryexpansionlayouthd.json'
TIP = 99999   # 说明框缺省在屏外，宿主按字的大小摆 | The tooltip box is off screen by default; the host positions it according to the size of the text

def widgets():
    return ('        { "type": "Widget", "name": "TouchPouchBox", "children": [\n'
            '            { "type": "ButtonWidget", "name": "TouchPouch", "fields": { "rect": { "x": 222, "y": 1294, "width": 280, "height": 128 }, "filename": "Controller/Panel/zspouch/PouchBox", '
            '"onClickMessage": "PanelManager:ClosePanel:TouchPouch" } },\n'
            '            { "type": "ImageWidget", "name": "TouchPouchIcon", "fields": { "rect": { "x": 246, "y": 1310, "scale": 0.95 }, "filename": "Items/misc/rune/el_rune" } },\n'
            '            { "type": "TextBoxWidget", "name": "TouchPouchCount", "fields": { "rect": { "x": 350, "y": 1294, "width": 140, "height": 128 }, "text": "0", '
            '"style": { "fontColor": "$FontColorGoldYellow", "pointSize": "$MediumLargeFontSize", "alignment": { "h": "left", "v": "center" } } } },\n'
            '            { "type": "ButtonWidget", "name": "TouchStore", "fields": { "rect": { "x": 916, "y": 1323, "width": 366, "scale": 0.74 }, "filename": "FrontEnd/HD/Final/FrontEnd_ButtonSmall", "hoveredFrame": 1, '
            '"textString": "@strGoldDeposit", "textColor": "$LightButtonTextColor", "text/style": "$StyleFEButtonText", "onClickMessage": "PanelManager:ClosePanel:TouchStore" } },\n'
            '            { "type": "NineTileImageWidget", "name": "TouchPouchTipBg", "fields": { "rect": { "x": %d, "y": 0, "width": 700, "height": 400 }, "filename": "PANEL/TooltipBorder/TooltipBorder", "backgroundColor": [0.0, 0.0, 0.0, 0.85], "tileScale": 1.0 } },\n'
            '            { "type": "TextBoxWidget", "name": "TouchPouchTip", "fields": { "rect": { "x": %d, "y": 0, "width": 900, "height": 600 }, "text": "", "fitToText": true, '
            '"style": { "fontColor": "$FontColorWhite", "pointSize": "$TooltipFontSize", "alignment": { "h": "center", "v": "top" }, "options": { "newlineHandling": "standard" } } } },\n'
            '        ] },\n') % (TIP, TIP)

def inv_layout(s):
    if '"TouchPouchBox"' in s: raise SystemExit('pouch_layout：物品栏布局里已经有 TouchPouchBox')
    i = s.rstrip().rfind(']'); assert s.rstrip().endswith('}') and i > 0
    return s[:i].rstrip(' ') + widgets() + '    ' + s[i:]

def build(romfs, outdir, repl):
    out = {}
    p = os.path.join(outdir, 'ws_pouchbox.sprite'); shutil.copyfile(os.path.join(__import__('gen_paths').gen_dir('NativeHUD', 'pouch'), 'PouchBox.sprite'), p); out['+data/hd/global/ui/controller/panel/zspouch/pouchbox.sprite'] = p
    logical = touch_layout.read_layout(romfs, INV)[0].lower(); src = repl.get(logical)
    text = open(src, encoding='utf-8').read() if src else touch_layout.read_layout(romfs, INV)[1]   # 叠在触摸布局已改的那份上（符文之语清单等） | Stacked on the copy already modified by the touch layout (runeword list etc.)
    p = os.path.join(outdir, 'pouch_' + os.path.basename(INV)); open(p, 'w', encoding='utf-8').write(inv_layout(text)); out[logical] = p
    return out, {'files': len(out)}
