# Maxwell（Tegra X1）着色器机器码里的资源引用：只解读绑定映射需要的那几类，不是完整反汇编器。
# 指令位模式取自 yuzu shader decode 表；代码段布局：图形阶段 = 0x30 NVN 头（0x12345678）+ 0x50 SPH + 指令，
# 计算阶段直接是指令；每 4 个 64 位字的第 1 个是调度控制字。
# 全量实测（10847 条变体，tools/shader_map.py）得出 NVN 绑定 ↔ D3D 寄存器：
#   uniform 缓冲  binding r ↔ cb_r，机器码里是 c[3 + r]（每阶段 14 个，c[3]..c[16]；更高的 c[22]/c[27] 是驱动内部）
#   分离贴图      slot r    ↔ t_r，句柄在 c[2][0x168 + 8r]（读法两种：c[2] 操作数，或贴图指令立即数贴图号 ×4）
#   分离采样器    slot r    ↔ s_r，句柄在 c[2][0x568 + 8r]（例外 1 条：cs_terrain_composite 多用 s2，PC 无）
#   存储缓冲      slot r    ↔ u_r 或只读结构化/原始缓冲 t_r（同一编号空间），地址/大小在 c[0][基址 + 0x10 r]
#                 基址：顶点 0x110、计算 0x310、像素 0x510；10847 条双向全等
# Resource references in Maxwell (Tegra X1) shader machine code: only decodes the few kinds needed for the binding mapping; not a full disassembler.
# Instruction bit patterns are taken from the yuzu shader decode table; code segment layout: graphics stage = 0x30 NVN header (0x12345678) + 0x50 SPH + instructions,
# compute stage is instructions directly; the 1st of every 4 64-bit words is the scheduling control word.
# A full measurement (10847 variants, tools/shader_map.py) gives NVN binding ↔ D3D register:
#   uniform buffer    binding r ↔ cb_r, which is c[3 + r] in machine code (14 per stage, c[3]..c[16]; the higher c[22]/c[27] are driver-internal)
#   separate texture  slot r    ↔ t_r, handle at c[2][0x168 + 8r] (two ways to read it: a c[2] operand, or the texture instruction's immediate texture index ×4)
#   separate sampler  slot r    ↔ s_r, handle at c[2][0x568 + 8r] (1 exception: cs_terrain_composite additionally uses s2, absent on PC)
#   storage buffer    slot r    ↔ u_r or read-only structured/raw buffer t_r (same numbering space), address/size at c[0][base + 0x10 r]
#                     base: vertex 0x110, compute 0x310, pixel 0x510; all 10847 match exactly in both directions
import struct

TEX_BASE, SAMP_BASE, UBO_FIRST, UBO_LAST = 0x168, 0x568, 3, 16
SSBO_BASE = {'vs': 0x110, 'cs': 0x310, 'ps': 0x510}

def instrs(code, graphics):
    off = 0x80 if graphics and code[:4] == bytes.fromhex('78563412') else 0
    out = []
    for k in range(off, len(code) - 7, 8):
        if ((k - off) // 8) % 4 == 0: continue
        out.append(struct.unpack_from('<Q', code, k)[0])
    return out

def match(w, pat):
    for i, ch in enumerate(pat):
        if ch != '-' and int(ch) != (w >> (63 - i)) & 1: return False
    return True

def cbuf_alu(ins):
    """操作数类为常量缓冲的 ALU 指令（最高 4 位 = 0100）：{(索引, 字节偏移)} | ALU instructions whose operand class is constant buffer (top 4 bits = 0100): {(index, byte offset)}"""
    return {((w >> 34) & 0x1f, ((w >> 20) & 0x3fff) * 4) for w in ins if (w >> 60) == 0x4 and w}

def ldc(ins):
    """LDC（按下标读常量缓冲）：{(索引, 字节偏移)} | LDC (read constant buffer by index): {(index, byte offset)}"""
    return {((w >> 36) & 0x1f, (w >> 20) & 0xffff) for w in ins if match(w, '1110111110010---')}

def bindings(code, stage):
    ins = instrs(code, stage != 'cs')
    refs = cbuf_alu(ins) | ldc(ins)
    ubo = {i - UBO_FIRST for i, _ in refs if UBO_FIRST <= i <= UBO_LAST}
    c2 = {o for i, o in refs if i == 2}
    # 立即数贴图号（TEX/TEXS/TLD/TLDS/TLD4/TLD4S 的 bits 36..48）= 句柄在 c[2] 里的字偏移，×4 即字节偏移（内嵌着色器 TLDS 0x6e→t10 核实）
    # Immediate texture index (bits 36..48 of TEX/TEXS/TLD/TLDS/TLD4/TLD4S) = word offset of the handle within c[2]; ×4 gives the byte offset (verified with the embedded shader's TLDS 0x6e→t10)
    for w in ins:
        for pat in ('110000----111---', '110010----111---', '11011100--11----', '1101111100------', '1101-00---------', '1101-01---------'):
            if match(w, pat): c2.add(((w >> 36) & 0x1fff) * 4); break
    tex = {(o - TEX_BASE) // 8 for o in c2 if TEX_BASE <= o < SAMP_BASE and (o - TEX_BASE) % 8 == 0}
    samp = {(o - SAMP_BASE) // 8 for o in c2 if o >= SAMP_BASE and (o - SAMP_BASE) % 8 == 0}
    base = SSBO_BASE.get(stage)
    ssbo = {(o - base) // 16 for i, o in refs if i == 0 and base and o >= base and (o - base) % 16 == 0 and (o - base) // 16 < 16} if base else set()
    drv = sorted(o for o in c2 if o < TEX_BASE)
    return {'ubo': ubo, 'tex': tex, 'samp': samp, 'ssbo': ssbo, 'c2drv': drv}

def vs_inputs(code):
    """顶点着色器读取的通用属性槽：ALD（1110111111011---）的属性地址 bits 20..29，通用属性 n 在 0x80 + 16n | Generic attribute slots read by the vertex shader: attribute address bits 20..29 of ALD (1110111111011---); generic attribute n is at 0x80 + 16n"""
    ins = instrs(code, True)
    return {((w >> 20) & 0x3ff) - 0x80 >> 4 for w in ins if match(w, '1110111111011---') and 0x80 <= ((w >> 20) & 0x3ff) < 0x280}

def sph_stage(code):
    """图形着色器 SPH 头的阶段字段（第 0 字 bits 10..13）：1 顶点、5 像素 | Stage field of the graphics shader SPH header (word 0, bits 10..13): 1 vertex, 5 pixel"""
    if code[:4] != bytes.fromhex('78563412'): return None
    return {1: 'vs', 5: 'ps'}.get((struct.unpack_from('<I', code, 0x30)[0] >> 10) & 0xf)
