#!/usr/bin/env python3
"""生成独立 Vulkan 像素测试的着色器与反射，不使用游戏内容。
Generate shader/reflection fixtures for independent Vulkan pixel tests, without game content.
"""
from pathlib import Path
import hashlib
import struct
import subprocess
import sys

output = Path(sys.argv[1]); output.mkdir(parents=True, exist_ok=True)
glslc = Path(sys.argv[2])
sources = [
    ("vert", "#version 450\nlayout(location=0) out vec2 uv;void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}", 0, 0, []),
    ("frag", "#version 450\nlayout(location=0) in vec2 uv;layout(set=1,binding=1) uniform sampler2D t;layout(location=0) out vec4 color;void main(){color=texture(t,uv);}", 1, 1, [(3,1,0,2,0,0x168//4,3,1,0,0)]),
    ("comp", "#version 450\nlayout(local_size_x=8,local_size_y=8) in;layout(set=0,binding=1,rgba8) uniform writeonly image2D target;void main(){imageStore(target,ivec2(gl_GlobalInvocationID.xy),vec4(10.0/255.0,80.0/255.0,220.0/255.0,1));}", 5, 0, [(4,1,0,2,0,0x120//4,3,1,4,0)]),
    ("frag", "#version 450\nlayout(set=1,binding=1) uniform Colors { vec4 rgba; };layout(location=0) out vec4 color;void main(){color=rgba;}", 1, 1, [(1,1,3,0,0,0,0,1,0,0)]),
    ("comp", "#version 450\nlayout(local_size_x=1) in;layout(set=0,binding=1) buffer Value { uint value; };void main(){value+=7;}", 5, 0, [(2,1,0,0,0x310//4,0,0,1,1,0)]),
    ("vert", "#version 450\nlayout(location=0) in vec2 position;void main(){gl_Position=vec4(position,0,1);}", 0, 0, []),
    ("frag", "#version 450\nlayout(set=1,binding=1) readonly buffer Colors { vec4 rgba; };layout(location=0) out vec4 color;void main(){color=rgba;}", 1, 1, [(2,1,0,0,0x510//4,0,0,1,0,0)]),
    ("frag", "#version 450\nlayout(set=1,binding=1) uniform samplerBuffer colors;layout(location=0) out vec4 color;void main(){color=texelFetch(colors,0);}", 1, 1, [(3,1,0,2,0,0x168//4,2,1,0,0)]),
    ("comp", "#version 450\nlayout(local_size_x=1) in;layout(set=0,binding=1,rgba32f) uniform writeonly imageBuffer colors;void main(){imageStore(colors,0,vec4(10.0/255.0,80.0/255.0,220.0/255.0,1));}", 5, 0, [(4,1,0,2,0,0x120//4,2,1,4,0)]),
    ("comp", "#version 450\nlayout(local_size_x=1) in;layout(set=0,binding=1) uniform Driver { uvec4 params[4096]; };layout(set=0,binding=2) buffer Value { uvec4 result; };void main(){result=params[0x210/16];}", 5, 0, [(1,1,0,0,0,0,0,1,0,0),(2,2,0,0,0x310//4,0,0,1,1,0)]),
    ("vert", "#version 450\nvoid main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);gl_Position=vec4(p*2.0-1.0,0.5,1);}", 0, 0, []),
    ("frag", "#version 450\nlayout(set=1,binding=1) uniform sampler2D t;layout(location=0) out vec4 color;void main(){color=texture(t,vec2(-0.25,0.5));}", 1, 1, [(3,1,0,2,0,0x168//4,3,1,0,0)]),
    ("frag", "#version 450\nlayout(set=1,binding=1) uniform sampler2DShadow t;layout(location=0) out vec4 color;void main(){float v=texture(t,vec3(-0.25,0.5,0.5));color=vec4(v,v,v,1);}", 1, 1, [(3,1,0,2,0,0x168//4,3|1024,1,0,0)]),
    # 真正游戏反序 handle：c2 的 sampler0(0x568) | texture10(0x1b8)。
    # Real reversed game handle: c2 sampler0(0x568) OR texture10(0x1b8).
    ("frag", "#version 450\nlayout(set=1,binding=1) uniform sampler2D t;layout(location=0) out vec4 color;void main(){color=texture(t,vec2(-0.25,0.5));}", 1, 1, [(3,1,0,0x30002,0,0x101b815a,3,1,0,0)]),
    # 实际阴影维度：array/cube，显式 LOD 和 gather 的比较结果同时写入独立颜色通道。
    # Real shadow dimensions: array/cube, with explicit LOD and gather comparisons in independent color channels.
    ("frag", "#version 450\nlayout(set=1,binding=1) uniform sampler2DArrayShadow t;layout(set=1,binding=2) uniform Params { vec4 coord; vec4 opts; };layout(location=0) out vec4 color;void main(){float explicit_lod=opts.x;float scale=exp2(explicit_lod)/8;float v=textureGrad(t,coord,vec2(scale,0),vec2(0,scale));vec4 g=textureGather(t,coord.xyz,coord.w);color=vec4(v,(g.x+g.y)*0.5,(g.z+g.w)*0.5,1);}", 1, 1, [(3,1,0,2,0,0x168//4,3|256|1024,1,0,0),(1,2,3,0,0,0,0,1,0,0)]),
    ("frag", "#version 450\nlayout(set=1,binding=1) uniform samplerCubeShadow t;layout(set=1,binding=2) uniform Params { vec4 coord; vec4 opts; };layout(location=0) out vec4 color;void main(){float explicit_lod=opts.x;float scale=exp2(explicit_lod)/8;float a=texture(t,coord);float b=textureGrad(t,coord,vec3(scale,0,0),vec3(0,scale,0));vec4 g=textureGather(t,coord.xyz,coord.w);color=vec4(a,b,dot(g,vec4(0.25)),1);}", 1, 1, [(3,1,0,2,0,0x168//4,5|1024,1,0,0),(1,2,3,0,0,0,0,1,0,0)]),
    # Packaged storage must read the actual CPU initial value before preserving G/B/A and adding red.
    ("comp", "#version 450\nlayout(local_size_x=8,local_size_y=8) in;layout(set=0,binding=1,rgba8) uniform image2D target;void main(){ivec2 p=ivec2(gl_GlobalInvocationID.xy);imageStore(target,p,imageLoad(target,p)+vec4(10.0/255.0,0,0,0));}", 5, 0, [(4,1,0,2,0,0x120//4,3,1,4,0)]),
]

def shadow_explicit_lod(path):
    # glslc 无 EXT_texture_shadow_lod 语法；Vulkan SPIR-V 原生支持该 Lod 操作数。
    # glslc lacks EXT_texture_shadow_lod syntax; Vulkan SPIR-V supports the native Lod operand.
    words = list(struct.unpack('<' + str(path.stat().st_size // 4) + 'I', path.read_bytes()))
    instructions = []; cursor = 5; lod_variable = None; lod_value = None
    while cursor < len(words):
        count = words[cursor] >> 16; opcode = words[cursor] & 65535
        if not count: raise ValueError('Zero-sized fixture SPIR-V instruction')
        inst = words[cursor:cursor + count]; instructions.append(inst); cursor += count
        if opcode == 5 and struct.pack('<' + str(count-2) + 'I', *inst[2:]).split(b'\0', 1)[0] == b'explicit_lod': lod_variable = inst[1]
    for inst in instructions:
        if inst[0] & 65535 == 62 and inst[1] == lod_variable: lod_value = inst[2]; break
    if lod_value is None: raise ValueError('Missing explicit shadow LOD fixture value')
    patched = 0; result = words[:5]
    for inst in instructions:
        if inst[0] & 65535 == 90 and len(inst) == 9 and inst[6] == 4:
            inst = inst[:6] + [2, lod_value]; inst[0] = len(inst) << 16 | 90; patched += 1
        result.extend(inst)
    if patched != 1: raise ValueError('Expected one shadow explicit-LOD fixture operation')
    path.write_bytes(struct.pack('<' + str(len(result)) + 'I', *result))

for index, (extension, source, stage, group, entries) in enumerate(sources, 1):
    key = hashlib.sha256(bytes([index]) + bytes(0x87f)).hexdigest()[:16]
    path = output / (key + "." + extension); path.write_text(source)
    subprocess.run([str(glslc), "--target-env=vulkan1.1", str(path), "-o", str(output / (key + ".spv"))], check=True)
    if index in (15, 16): shadow_explicit_lod(output / (key + '.spv'))
    bindings = [(0,0,0,0,0,0,0,1,0,0)] + entries
    header = struct.pack("<4s6I", b"DMVK", 1, stage, group, len(bindings), 0, 32)
    attributes = struct.pack("<32I", 1, *([0]*31)) if index == 6 else bytes(128)
    (output / (key + ".meta")).write_bytes(header + b"".join(struct.pack("<10I", *b) for b in bindings) + attributes)
