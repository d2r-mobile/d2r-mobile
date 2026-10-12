# 片元着色器的「可读 tile」版本（合并 B 第 2 步）：同一通道里，后一次绘制本像素采样前一次绘制写的颜色附件时，
# 改成直接读 tile 内的颜色（framebuffer fetch），两个渲染通道合成一个，中间附件不必为被采样而落主存再读回。
# 离线出包、运行时只做函数常量特化（不编译源码）。用法：variant(msl, bindings, om_only_ok=False) → (新 MSL, 元数据) 或 (None, 原因)
# 函数常量编号：FX_i（第 i 次可改采样读第几号颜色，-1 = 照常采样）= i；FB_k（启用第 k 号颜色输入）= 32 + k；
#               OM_j_k（第 j 个输出写到第 k 号附件）= 64 + 8 j + k
# 可改的采样：2D 浮点贴图的 .sample(采样器, select(_N, float2(0.0), isnan(_N)))，且 _N = float2(in.m_X.x, in.m_X.y)（插值输出原样）；
# 是否真在本像素取纹素由运行时证明（顶点 uv 与位置的对应、贴图尺寸 = 目标尺寸），证不出就用原版本
# "Readable-tile" version of a fragment shader (merge B step 2): within one pass, when a later draw samples, at its own pixel, a color attachment written by an earlier draw,
# it is changed to read the color directly from the tile (framebuffer fetch); the two render passes become one, and the intermediate attachment need not be stored to main memory and loaded back just to be sampled.
# Built offline; at runtime only function-constant specialization is done (no source compilation). Usage: variant(msl, bindings, om_only_ok=False) → (new MSL, metadata) or (None, reason)
# Function constant numbering: FX_i (which color index the i-th rewritable sample reads, -1 = sample as usual) = i; FB_k (enable color input k) = 32 + k;
#               OM_j_k (the j-th output writes to attachment k) = 64 + 8 j + k
# Rewritable samples: .sample(sampler, select(_N, float2(0.0), isnan(_N))) on a 2D float texture, where _N = float2(in.m_X.x, in.m_X.y) (interpolated output unchanged);
# whether the texel is really fetched at this pixel is proven at runtime (correspondence between vertex uv and position, texture size = target size); if it cannot be proven, the original version is used
import re

NFB = 8

def variant(msl, bindings, om_only_ok=False):
    tex = {m.group(1) for m in re.finditer(r'texture2d<float> (\w+) \[\[id\(\d+\)\]\];', msl)}
    locn = {m.group(1): int(m.group(2)) for m in re.finditer(r'float4 (m_\d+) \[\[user\(locn(\d+)\)\]\];', msl.split('struct main0_in', 1)[-1].split('};', 1)[0])}
    reg = {b['msl_name']: b['reg'] for b in bindings if b.get('kind') == 'tex' and b.get('msl_name') and b.get('reg') is not None}
    pat = re.compile(r'spvDescriptorSet0\.(\w+)\.sample\(spvDescriptorSet0\.(\w+)Smplr, select\((_\d+), float2\(0\.0\), isnan\(\3\)\)\)')
    calls, out_parts, pos = [], [], 0
    for m in pat.finditer(msl):
        member, var = m.group(1), m.group(3)
        if member not in tex or member != m.group(2) or member not in reg: continue
        d = re.search(r'float2 %s = float2\((_\d+), (_\d+)\);' % var, msl)
        a = d and re.search(r'float %s = in\.(m_\d+)\.x;' % d.group(1), msl); b = d and re.search(r'float %s = in\.(m_\d+)\.y;' % d.group(2), msl)
        if not (a and b) or a.group(1) != b.group(1) or a.group(1) not in locn: continue
        i = len(calls)
        if i >= 32: break
        sel = ''.join(f'FX_{i} == {k} ? fb{k} : ' for k in range(NFB))
        out_parts.append(msl[pos:m.start()]); out_parts.append(f'({sel}{m.group(0)})'); pos = m.end()
        calls.append({'fc': i, 'tex_reg': reg[member], 'varying_locn': locn[a.group(1)]})
    if not calls and not om_only_ok:
        return None, '没有可改的采样'
    # 混用的槽：同一贴图还有别的（不可改的）用法——运行时不得把这张贴图改成读 tile（改了的几次与没改的读到的会不是同一份数据）
    # Mixed-use slots: the same texture also has other (non-rewritable) uses - at runtime this texture must not be switched to tile reads (the rewritten reads and the unrewritten ones would not see the same data)
    conv = {}
    for c in calls: conv[c['tex_reg']] = conv.get(c['tex_reg'], 0) + 1
    mixed = sorted({reg[mb] for mb in reg if mb in tex and msl.count(f'spvDescriptorSet0.{mb}.') > conv.get(reg[mb], 0)})
    body = ''.join(out_parts) + msl[pos:]
    om = re.search(r'struct main0_out\s*\{(.*?)\};', body, re.S)
    if not om: return None, '没有输出结构'
    outs = [(t, n, int(c)) for t, n, c in re.findall(r'(\w+) (m_\d+) \[\[color\((\d+)\)\]\];', om.group(1))]
    if not outs or len(outs) > 4 or re.sub(r'\w+ m_\d+ \[\[color\(\d+\)\]\];', '', om.group(1)).strip(): return None, '输出结构不是单纯的颜色输出（或超过 4 个）'
    members = ''.join(f'    {t} {n}_at{k} [[color({k}), function_constant(OM_{j}_{k})]];\n' for j, (t, n, c) in enumerate(outs) for k in range(NFB))
    body = body[:om.start(1)] + '\n' + members + body[om.end(1):]
    consts = ''.join(f'constant int FX_{c["fc"]} [[function_constant({c["fc"]})]];\n' for c in calls) + \
             ''.join(f'constant bool FB_{k} [[function_constant({32 + k})]];\n' for k in range(NFB)) + \
             ''.join(f'constant bool OM_{j}_{k} [[function_constant({64 + 8 * j + k})]];\n' for j in range(len(outs)) for k in range(NFB))
    body = body.replace('using namespace metal;\n', 'using namespace metal;\n\n' + consts, 1)
    i0 = body.find('fragment main0_out main0(')
    i1 = body.find(')\n{', i0) if i0 >= 0 else -1   # 签名结尾：右括号后紧跟换行与左花括号（参数属性里有 [[buffer(0)]] 等括号） | end of signature: closing parenthesis immediately followed by a newline and an opening brace (parameter attributes contain brackets such as [[buffer(0)]])
    if i0 < 0 or i1 < 0: return None, '主函数签名认不出'
    body = body[:i1] + ''.join(f', float4 fb{k} [[color({k}), function_constant(FB_{k})]]' for k in range(NFB)) + body[i1:]
    decl = ''.join(f'    {t} spv_o_{n} = {t}(0);\n' for t, n, c in outs)
    body = body.replace('    main0_out out = {};\n', '    main0_out out = {};\n' + decl, 1)
    for t, n, c in outs: body = body.replace(f'out.{n}', f'spv_o_{n}')
    put = ''.join(f'    if (OM_{j}_{k}) out.{n}_at{k} = spv_o_{n};\n' for j, (t, n, c) in enumerate(outs) for k in range(NFB))
    if body.count('    return out;\n') < 1: return None, '找不到 return out'
    body = body.replace('    return out;\n', put + '    return out;\n')
    return body, {'calls': calls, 'mixed_regs': mixed, 'outputs': [{'color': c, 'type': t} for t, n, c in outs]}
