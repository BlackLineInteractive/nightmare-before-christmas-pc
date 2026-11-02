#!/usr/bin/env python3
"""
Xbox (NV2A) pixel shaders of default.xbe -> HLSL for Direct3D 9.

An Xbox pixel shader is a D3DPIXELSHADERDEF: the register-combiner setup of
the NV2A (up to 8 general combiner stages, RGB and alpha, plus the final
combiner). The game creates 8 of them from a table in .data (0x3185a4, loop
at 0x1a2350) and hands 2 more straight to SetPixelShaderProgram (0x1bcba6,
0x1bcdb8). They carry the game's brightness: e.g. R0 = T0 + T0, R0 * V0 is
texture x 2 x vertex colour, which Direct3D 9 texture stages left at half.
Each one is translated once, at build time, to ps_2_0 HLSL.

Field layout follows the public D3DPIXELSHADERDEF / NV097 combiner register
descriptions (XDK d3d8types.h, xemu psh.c).

Not covered yet: fog (FOG is (0, 0, 0, 1), i.e. no fog), texture modes other
than 2D (linear textures: texel coordinates, scaled by c0..c3) (all the game's shaders use PROJECT2D), per-draw SetPixelShaderConstant.

usage: nv2a_psh.py [default.xbe]   (prints the decoded shaders and HLSL)
"""

import struct
import sys

import nv2a_vsh

TABLE = 0x3185a4
TABLE_COUNT = 8
DIRECT = [0x27c250, 0x27c160]             # SetPixelShaderProgram(def) call sites

# D3DPIXELSHADERDEF field offsets
ALPHA_IN, FINAL_ABCD, FINAL_EFG = 0x00, 0x20, 0x24
CONST0, CONST1, ALPHA_OUT, RGB_IN = 0x28, 0x48, 0x68, 0x88
FINAL_C0, FINAL_C1, RGB_OUT, COUNT, TEX_MODES = 0xAC, 0xB0, 0xB4, 0xD4, 0xD8
DEF_SIZE = 0xF0

REG = {0: 'ZERO', 1: 'C0', 2: 'C1', 3: 'FOG', 4: 'V0', 5: 'V1', 8: 'T0', 9: 'T1', 10: 'T2', 11: 'T3',
       12: 'R0', 13: 'R1', 14: 'V1R0', 15: 'EF'}
WRITABLE = {4, 5, 8, 9, 10, 11, 12, 13}
MAPPING = ['max({x}, 0)', '(1 - saturate({x}))', '(2 * max({x}, 0) - 1)', '(1 - 2 * max({x}, 0))',
           '(max({x}, 0) - 0.5)', '(0.5 - max({x}, 0))', '{x}', '(-{x})']
OUT_OP = {0: '{x}', 1: '({x} - 0.5)', 2: '({x} * 2)', 3: '(({x} - 0.5) * 2)', 4: '({x} * 4)', 6: '({x} * 0.5)'}


def color(argb):
    a, r, g, b = (argb >> 24) & 255, (argb >> 16) & 255, (argb >> 8) & 255, argb & 255
    return f"float4({r / 255:.6f}, {g / 255:.6f}, {b / 255:.6f}, {a / 255:.6f})"


def read_def(read_u32, va):
    return [read_u32(va + o) for o in range(0, DEF_SIZE, 4)]


def u(d, off):
    return d[off // 4]


def src(byte, rgb, consts):
    """one combiner input (8 bits: register, alpha select, mapping) -> HLSL expression"""
    reg, alpha, mapping = byte & 15, (byte >> 4) & 1, (byte >> 5) & 7
    name = consts.get(reg, REG.get(reg, 'ZERO'))
    if rgb:
        x = f"{name}.aaa" if alpha else f"{name}.rgb"
    else:
        x = f"{name}.a" if alpha else f"{name}.b"
    return MAPPING[mapping].format(x=x)


def stage(d, i, consts, msb):
    """HLSL for general combiner stage i (RGB and alpha read the same inputs, then write)"""
    lines, writes = [], []
    for rgb in (True, False):
        inp = u(d, (RGB_IN if rgb else ALPHA_IN) + 4 * i)
        out = u(d, (RGB_OUT if rgb else ALPHA_OUT) + 4 * i)
        A, B, C, D = [src((inp >> s) & 255, rgb, consts) for s in (24, 16, 8, 0)]
        t, sw = ('float3', 'rgb') if rgb else ('float', 'a')
        p = f"{sw[0]}{i}"
        ab = f"dot({A}, {B}).xxx" if rgb and out & (1 << 13) else f"{A} * {B}"
        cd = f"dot({C}, {D}).xxx" if rgb and out & (1 << 12) else f"{C} * {D}"
        op = OUT_OP.get((out >> 15) & 7, '{x}')
        lines.append(f"  {t} {p}ab = clamp({op.format(x=f'({ab})')}, -1, 1);")
        lines.append(f"  {t} {p}cd = clamp({op.format(x=f'({cd})')}, -1, 1);")
        if out & (1 << 14):
            # mux: R0.a's most significant bit, or its lowest bit (as 8-bit fixed point)
            sel = "R0.a >= 0.5" if msb else "fmod(round(saturate(R0.a) * 255), 2) >= 1"
            s = f"(({sel}) ? {p}cd : {p}ab)"
        else:
            s = f"({p}ab + {p}cd)"
        lines.append(f"  {t} {p}sum = clamp({s}, -1, 1);")
        for field, val in ((4, f"{p}ab"), (0, f"{p}cd"), (8, f"{p}sum")):
            dst = (out >> field) & 15
            if dst in WRITABLE:
                writes.append(f"  {REG[dst]}.{sw} = {val};")
        if rgb:   # AB / CD blue to alpha
            if out & (1 << 19) and (out >> 4) & 15 in WRITABLE:
                writes.append(f"  {REG[(out >> 4) & 15]}.a = {p}ab.b;")
            if out & (1 << 18) and out & 15 in WRITABLE:
                writes.append(f"  {REG[out & 15]}.a = {p}cd.b;")
    return lines + writes


def hlsl(d):
    count = u(d, COUNT)
    # PS_COMBINERCOUNT: stages in bits 0..7, MUX_MSB 0x100, UNIQUE_C0 0x1000, UNIQUE_C1 0x10000
    n, msb, unique_c0, unique_c1 = count & 255, count & 0x100, count & 0x1000, count & 0x10000
    modes = u(d, TEX_MODES)
    s = "sampler2D s0 : register(s0);\nsampler2D s1 : register(s1);\n"
    s += "sampler2D s2 : register(s2);\nsampler2D s3 : register(s3);\n"
    s += ("struct PSIN { float4 d0 : COLOR0; float4 d1 : COLOR1; float4 t0 : TEXCOORD0; "
          "float4 t1 : TEXCOORD1; float4 t2 : TEXCOORD2; float4 t3 : TEXCOORD3; };\n")
    # Xbox linear textures take texel coordinates; the renderer sets 1 / size for them, 1 otherwise
    s += "float4 tscale[4] : register(c0);\n"
    s += "float4 main(PSIN i) : COLOR0\n{\n"
    s += "  float4 ZERO = 0, FOG = float4(0, 0, 0, 1);\n"
    s += "  float4 V0 = i.d0, V1 = i.d1;\n"
    for t in range(4):
        mode = (modes >> (5 * t)) & 31
        s += f"  float4 T{t} = {f'tex2D(s{t}, i.t{t}.xy * tscale[{t}].xy)' if mode else '0'};\n"
    s += "  float4 R0 = float4(0, 0, 0, T0.a), R1 = 0;\n"
    for i in range(min(n, 8)):
        c0 = u(d, CONST0 + 4 * (i if unique_c0 else 0))
        c1 = u(d, CONST1 + 4 * (i if unique_c1 else 0))
        s += f"  float4 C0_{i} = {color(c0)}, C1_{i} = {color(c1)};\n"
        s += '\n'.join(stage(d, i, {1: f'C0_{i}', 2: f'C1_{i}'}, msb)) + '\n'
    # final combiner: rgb = A*B + (1-A)*C + D, alpha = G; EF = E*F, V1R0 = V1 + R0
    abcd, efg = u(d, FINAL_ABCD), u(d, FINAL_EFG)
    fc = {1: 'FC0', 2: 'FC1'}
    s += f"  float4 FC0 = {color(u(d, FINAL_C0))}, FC1 = {color(u(d, FINAL_C1))};\n"
    v1 = '(1 - V1.rgb)' if efg & 0x40 else 'V1.rgb'
    r0 = '(1 - R0.rgb)' if efg & 0x20 else 'R0.rgb'
    vsum = f"{v1} + {r0}"
    s += f"  float4 V1R0 = float4({'saturate(' + vsum + ')' if efg & 0x80 else vsum}, 0);\n"
    E, F = [src((efg >> sh) & 255, True, fc) for sh in (24, 16)]
    s += f"  float4 EF = float4({E} * {F}, 0);\n"
    A, B, C, D = [src((abcd >> sh) & 255, True, fc) for sh in (24, 16, 8, 0)]
    G = src((efg >> 8) & 255, False, fc)
    s += f"  float3 fa = {A};\n"
    s += f"  return float4(saturate(fa * {B} + (1 - fa) * {C} + {D}), saturate({G}));\n}}\n"
    return s


def read_shaders(read_u32):
    """-> [(definition address, words)]: the table entries, then the direct ones."""
    addrs = [read_u32(TABLE + 4 * i) for i in range(TABLE_COUNT)] + DIRECT
    return [(va, read_def(read_u32, va)) for va in addrs]


def emit_c(read_u32, out_path):
    """build/pc/pshaders.inc: definition address and HLSL of each shader."""
    shaders = read_shaders(read_u32)
    out = [f'#define PSH_COUNT {len(shaders)}',
           'static const u32 g_psh_def[PSH_COUNT] = { ' + ', '.join(f"{va:#x}" for va, _ in shaders) + ' };',
           'static const char* const g_psh_src[PSH_COUNT] = {']
    for _, d in shaders:
        out.append('    ' + '\n    '.join('"' + l.replace('\\', '\\\\').replace('"', '\\"') + '\\n"'
                                         for l in hlsl(d).splitlines()) + ',')
    out.append('};')
    with open(out_path, 'w') as f:
        f.write('\n'.join(out) + '\n')


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'rom/extracted/default.xbe'
    for va, d in read_shaders(nv2a_vsh.xbe_reader(path)):
        print(f"==== pixel shader {va:#x}: {u(d, COUNT) & 255} stages, texture modes {u(d, TEX_MODES):#x}")
        print(hlsl(d))


if __name__ == '__main__':
    main()
