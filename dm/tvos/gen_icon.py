#!/usr/bin/env python3
# Apple TV 图标：由 iOS 的 1024² 图标生成 tvOS 要的分层图标（AppIcon.brandassets：小 400×240 @1x/@2x、大 1280×768），不另存一份图。
# 两层：背景 = 图标四角取的底色铺满；前景 = 图标按高度缩放、水平居中（透明边），焦点视差时前景浮在背景上。
# 用法：gen_icon.py <iOS 图标 png> <输出 .xcassets 目录>。输入没变就不重写（Xcode 构建前脚本每次都调）
# Apple TV icon: generates the layered icon tvOS needs (AppIcon.brandassets: small 400×240 @1x/@2x, large 1280×768) from the iOS 1024² icon; no separate copy of the image is kept.
# Two layers: background = fill with the base color sampled from the icon corners; foreground = icon scaled by height, horizontally centered (transparent margins), floating over the background during focus parallax.
# Usage: gen_icon.py <iOS icon png> <output .xcassets directory>. Not rewritten if the input is unchanged (the Xcode pre-build script calls it every time)
import json, os, sys, hashlib
from PIL import Image
src, out = sys.argv[1], sys.argv[2]
digest = hashlib.sha256(open(src, 'rb').read() + open(__file__, 'rb').read()).hexdigest()
stamp = os.path.join(out, '.src.sha256')
if os.path.exists(stamp) and open(stamp).read() == digest: sys.exit(0)
icon = Image.open(src).convert('RGBA')
w0, h0 = icon.size
corner = icon.getpixel((2, 2))

def put(path, obj):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f: json.dump(obj, f, indent=2)

def stack(name, w, h, scales):
    root = os.path.join(out, 'AppIcon.brandassets', name + '.imagestack')
    put(os.path.join(root, 'Contents.json'), {'info': {'version': 1, 'author': 'xcode'}, 'layers': [{'filename': 'Front.imagestacklayer'}, {'filename': 'Back.imagestacklayer'}]})
    for layer in ('Front', 'Back'):
        d = os.path.join(root, layer + '.imagestacklayer')
        put(os.path.join(d, 'Contents.json'), {'info': {'version': 1, 'author': 'xcode'}})
        images = []
        for s in scales:
            W, H = w * s, h * s
            if layer == 'Back': img = Image.new('RGBA', (W, H), corner)
            else:
                img = Image.new('RGBA', (W, H), (0, 0, 0, 0))
                side = H; fg = icon.resize((side * w0 // h0, side), Image.LANCZOS)
                img.paste(fg, ((W - fg.width) // 2, 0))
            fn = f'{layer.lower()}@{s}x.png'
            os.makedirs(os.path.join(d, 'Content.imageset'), exist_ok=True)
            img.save(os.path.join(d, 'Content.imageset', fn))
            images.append({'idiom': 'tv', 'filename': fn, 'scale': f'{s}x'})
        put(os.path.join(d, 'Content.imageset', 'Contents.json'), {'images': images, 'info': {'version': 1, 'author': 'xcode'}})

put(os.path.join(out, 'Contents.json'), {'info': {'version': 1, 'author': 'xcode'}})
put(os.path.join(out, 'AppIcon.brandassets', 'Contents.json'), {'info': {'version': 1, 'author': 'xcode'}, 'assets': [
    {'size': '400x240', 'idiom': 'tv', 'filename': 'Small.imagestack', 'role': 'primary-app-icon'},
    {'size': '1280x768', 'idiom': 'tv', 'filename': 'Large.imagestack', 'role': 'primary-app-icon'}]})
stack('Small', 400, 240, (1, 2))
stack('Large', 1280, 768, (1,))
open(stamp, 'w').write(digest)
