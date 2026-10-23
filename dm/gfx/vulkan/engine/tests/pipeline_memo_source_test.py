# 核对生产State完整字段清单及上一draw管线memo的失效接线；补足ABI padding内新增字段无法靠sizeof发现的情况。
# Check the full production State inventory and the previous-draw pipeline memo's invalidation wiring, including new fields
# hidden inside ABI padding.
# memo键按指针记着色器、布局定义和pass定义（xbg_game.cpp PipeMemoKey）；这些对象只在关闭时释放，且先于释放清掉memo。
# The memo key holds shader, layout-definition and pass-definition pointers (xbg_game.cpp PipeMemoKey); those objects are freed
# only at shutdown, after the clear_* calls that drop the memo.
from pathlib import Path
import re
import sys

sys.dont_write_bytecode = True
from texture_cache_regression import balanced

root = Path(__file__).resolve().parent.parent
source = (root / 'xbg_game.cpp').read_text(encoding='utf-8')
start = source.index('struct State {')
body = source[source.index('{', start) + 1:source.index('\n} state;', start)]
while '{' in body:
    next_body = re.sub(r'\{[^{}]*\}', '', body)
    assert next_body != body, 'Cannot parse State initializer; audit the inventory checker'
    body = next_body
body = re.sub(r'\[[^]]*\]|=[^,;]+', '', body)
names = [re.findall(r'[A-Za-z_]\w*', part)[-1]
         for declaration in body.split(';') if declaration.strip()
         for part in declaration.split(',')]
expected = ['rt', 'shader', 'ub', 'sb', 'vb', 'tex', 'samp', 'img', 'texture_pool', 'sampler_pool',
            'attrib', 'stream', 'blend', 'color', 'mask', 'ms', 'poly', 'ds', 'viewport', 'scissor',
            'blend_color', 'bias', 'stencil_compare', 'stencil_write', 'stencil_ref', 'copy_stride',
            'copy_image_stride', 'patch_size', 'bounds', 'count_samples', 'samples_passed']
assert names == expected, ('New or reordered State fields require explicit pipeline/dynamic/resource policy', names)
key = balanced(source, source.index('struct PipeMemoKey{'))
for part in ['offsetof(State,viewport)-offsetof(State,attrib)', 'offsetof(State,copy_stride)-offsetof(State,blend_color)',
             'offsetof(State,count_samples)-offsetof(State,patch_size)']:
    assert part in key, part
for signature, first_destroy in [('void clear_pipeline_objects(){', None),
                                  ('void clear_descriptor_caches(){', 'vkDestroyDescriptorUpdateTemplate'),
                                  ('void clear_render_pass_cache(){', None)]:
    block = balanced(source, source.index(signature))
    assert 'pipe_memo_valid=false' in block, signature
    if first_destroy:
        assert block.index('pipe_memo_valid=false') < block.index(first_destroy), signature
lookup = balanced(source, source.index('void pipeline_lookup('))
assert lookup.index('pipe_memo_valid=false') < lookup.index('pipeline_objects.erase(oldest)')
shutdown = balanced(source, source.index('extern "C" int xbg_game_shutdown(){'))
for call in ['clear_pipeline_objects()', 'clear_descriptor_caches()', 'clear_render_pass_cache()']:
    assert shutdown.index(call) < shutdown.index('shaders.clear()'), call
assert 'shaders.erase' not in source and source.count('shaders.clear()') == 1
assert 'render_pass_cache.erase' not in source and 'layout_cache.erase' not in source
assert '!ab_off("pipememo")' in source and '"/gfx-pipe-memo"' in source and 'pipe_memo_valid=false;if(m)' in source
print('PASS: 31 actual State fields, memo key slices, clear/evict/shutdown invalidation and opt-in/A-B source wiring; not a native or GPU test')
