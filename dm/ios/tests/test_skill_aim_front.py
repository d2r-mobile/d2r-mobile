"""最小真实源接线约束；不声称UIImage/物理输入/原施法效果。 | Minimal real-source wiring constraints; makes no claim about UIImage / physical input / the original cast effect."""
from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--ios-root',type=Path,default=Path(__file__).resolve().parents[1]);args=p.parse_args()
ios=args.ios_root
f=(ios/'Sources/front_ios.m').read_text()
assert 'dm_input_set(_controls.buttons,_controls.lx,_controls.ly,_controls.rx,_controls.ry)' in f, 'front still drops right axis'
assert f.count('rx=%d ry=%d')>=2, 'both UITouch and output must report actual right axes'
assert f.count('_controls.lx,_controls.ly,_controls.rx,_controls.ry')>=3
assert 'dm_tc_set_navigation_mode(&_controls,n)' in f, 'navigation must suppress combat aim'
m=(ios/'Sources/DmTouchOverlay.m').read_text()
assert '@"touch_native_binding_v5"' in m
assert 's->map_cancel_active' in m and '@"right_aim"' in m
d=ios/'docs/touch_layout_spec_native_binding_v5.json'
assert json.loads(d.read_text())['profile_id']=='touch_native_binding_v5'
assert hashlib.sha256(d.read_bytes()).hexdigest() in (ios/'Sources/dm_touch_native_art.c').read_text()
print('PASS v5 source seam: full five-axis publish; real rx/ry receipts; navigation/cancel metadata; v5 spec SHA; runtime/phone UNRUN')
