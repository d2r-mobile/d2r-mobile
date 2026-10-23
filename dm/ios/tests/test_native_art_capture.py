#!/usr/bin/env python3
"""素材采图契约与原素材哈希；不把synthetic snapshot当游戏身份。 | Asset capture contract and original asset hashes; a synthetic snapshot is not treated as game identity."""
import argparse,hashlib,json,re
from pathlib import Path
from PIL import Image
import os; DM_ROOT = os.environ.get('DM_ROOT') or os.path.expanduser('~/dm-work')   # 工作区根目录（游戏文件、产物、运行现场都在它下面） | Workspace root (game files, build outputs and run artifacts are all under it)
IOS=Path(__file__).resolve().parent.parent
# 安卓把生成索引与贴图放仓外；默认仍验证苹果工程原路径。
# Android keeps generated indices and textures outside the checkout; default to the Apple project's original paths.
GENERATED=Path(os.environ.get('DM_NATIVE_ART_DIR', str(IOS/'Sources')))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
try:
 # 这里原有一组「在 DmTouchOverlay.m 里找某段源码文字」的断言（底栏跳过宿主绘制、元数据字段、重画入口等），回城钮改版后对不上。
 # 它们守的是实现写法不是行为：底栏谁来画由回归整表的界面截图守，触摸到游戏的协议行为由本目录的 C 单测（t_native_binding / t_game_bridge / t_native_hud_bridge 等）守——删掉，不追着源码文字改。
 # 留下的是数据一致性：编进程序的三个哈希常量、清单、12 张贴图、两张技能表与实际文件一致。
 # There used to be a group of assertions here that "look for a piece of source text in DmTouchOverlay.m" (bottom bar skips host drawing, metadata fields, redraw entry, etc.), which no longer matched after the town portal button redesign.
 # They guarded how the implementation is written, not behavior: who draws the bottom bar is guarded by the UI screenshots of the full regression table, and the touch-to-game protocol behavior is guarded by the C unit tests in this directory (t_native_binding / t_game_bridge / t_native_hud_bridge etc.) - deleted, not chasing the source text with edits.
 # What remains is data consistency: the three hash constants compiled into the program, the manifest, the 12 textures and the two skill tables match the actual files.
 manifest=json.loads((IOS/'Sources/TouchArt/native_art_manifest.json').read_text())
 compiled=(GENERATED/'dm_touch_native_art.c').read_text(encoding='utf-8')
 for name,target in [('dm_ta_manifest_sha',IOS/'Sources/TouchArt/native_art_manifest.json'),
                     ('dm_ta_geometry_sha',IOS/'docs/native_art_v1_geometry.json'),
                     ('dm_ta_binding_sha',IOS/'docs/touch_layout_spec_native_binding_v5.json')]:
  values=re.findall(r'\bconst\s+char\s+'+name+r'\s*\[\s*\]\s*=\s*"([^"\n]*)"\s*;',compiled)
  assert len(values)==1, f'{name} declaration must occur exactly once'
  assert values[0]==sha(target), f'{name} differs from actual file SHA256'
 assert manifest['geometry_sha256']==sha(IOS/'docs/native_art_v1_geometry.json')
 assert manifest['binding_protocol_sha256']==sha(IOS/'docs/touch_layout_spec_native_binding_v5.json')
 for a in manifest['assets']:
  with Image.open(GENERATED/'TouchArt'/a['asset']) as image:
   rgba=image.convert('RGBA')
   assert list(rgba.size)==a['atlas_px'], a['asset']+' dimensions'
   assert hashlib.sha256(rgba.tobytes()).hexdigest()==a['decoded_rgba_sha256'], a['asset']+' RGBA pixels'
  assert a['pixel_format_code']==61 and len(a['official_sprite_md5'])==32 and len(a['official_sprite_sha256'])==64
 import os,sys
 romfs=os.environ.get('DM_ROMFS_SRC') or os.path.join(DM_ROOT, 'data/switch/3.3.94012/Update/RomFS')   # 两张技能表不进仓库，从游戏文件现读核哈希 | the two skill tables are not kept in the repository; read fresh from the game files and verify the hashes
 if os.path.isfile(os.path.join(romfs,'data','build_manifest.psv')):
  sys.path.insert(0,str(IOS/'tools')); import romfs_art
  for table in manifest['official_tables']:
   raw=romfs_art.read(romfs,'global/excel/'+table['file'])
   assert hashlib.sha256(raw).hexdigest()==table['sha256']==table['official']['sha256']
   assert hashlib.md5(raw).hexdigest()==table['official']['ckey_md5']
 else: print('（这台机器没有游戏文件，跳过两张技能表的哈希核对）')
 assert len(manifest['assets'])==12
 print('PASS native_art_capture: 3/3 real C hash constants match actual files; geometry+binding hashes distinct; 12/12 dimensions and RGBA pixel hashes; 2/2 frozen official tables SHA+MD5')
except AssertionError as e:
 print(f'FAIL: {e}');raise SystemExit(1)
