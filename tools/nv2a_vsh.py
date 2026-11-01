#!/usr/bin/env python3
"""
Xbox (NV2A) vertex shaders of default.xbe -> HLSL for Direct3D 9.

The game creates its 14 vertex shaders from a static table in .data
(declarations at 0x321d30, microcode at 0x321cf8; loop at 0x1a2480). This
module reads them straight from default.xbe and translates each program
once, at build time. The PC renderer compiles the HLSL with d3dcompiler.

NV2A instruction layout (128 bits, words 1..3 used) follows the public
descriptions of the Xbox vertex program format (xemu vsh.c, Cxbx-R).

usage: nv2a_vsh.py [default.xbe]   (prints the disassembly and HLSL)
"""

import struct
import sys

SHADER_TABLE_FN = 0x321cf8
SHADER_TABLE_DECL = 0x321d30
SHADER_COUNT = 14

MAC_OPS = ['NOP', 'MOV', 'MUL', 'ADD', 'MAD', 'DP3', 'DPH', 'DP4', 'DST', 'MIN', 'MAX', 'SLT', 'SGE', 'ARL']
ILU_OPS = ['NOP', 'MOV', 'RCP', 'RCC', 'RSQ', 'EXP', 'LOG', 'LIT']
OUT_REGS = {0: 'oPos', 3: 'oD0', 4: 'oD1', 5: 'oFog', 6: 'oPts', 7: 'oB0', 8: 'oB1',
            9: 'oT0', 10: 'oT1', 11: 'oT2', 12: 'oT3'}
SWZ = 'xyzw'


def field(w, lo, n):
    return (w >> lo) & ((1 << n) - 1)


def decode(words):
    """words: the 4 DWORDs of one instruction -> dict of fields."""
    _, d1, d2, d3 = words
    c_r = (field(d2, 0, 2) << 2) | field(d3, 30, 2)
    return dict(
        ilu=field(d1, 25, 3), mac=field(d1, 21, 4), const=field(d1, 13, 8), v=field(d1, 9, 4),
        a=dict(neg=field(d1, 8, 1), swz=[field(d1, 6, 2), field(d1, 4, 2), field(d1, 2, 2), field(d1, 0, 2)],
               r=field(d2, 28, 4), mux=field(d2, 26, 2)),
        b=dict(neg=field(d2, 25, 1), swz=[field(d2, 23, 2), field(d2, 21, 2), field(d2, 19, 2), field(d2, 17, 2)],
               r=field(d2, 13, 4), mux=field(d2, 11, 2)),
        c=dict(neg=field(d2, 10, 1), swz=[field(d2, 8, 2), field(d2, 6, 2), field(d2, 4, 2), field(d2, 2, 2)],
               r=c_r, mux=field(d3, 28, 2)),
        mac_mask=field(d3, 24, 4), out_r=field(d3, 20, 4), ilu_mask=field(d3, 16, 4),
        o_mask=field(d3, 12, 4), orb=field(d3, 11, 1), out_addr=field(d3, 3, 8),
        out_mux=field(d3, 2, 1), a0x=field(d3, 1, 1), final=field(d3, 0, 1),
    )


def mask_str(m):
    return ''.join(c for c, bit in zip('xyzw', (8, 4, 2, 1)) if m & bit)


# ------------------------------------------------------------------ HLSL --

def src_expr(op, ins):
    """HLSL expression (float4) for an operand."""
    mux = op['mux']
    if mux == 1:
        base = 'oPos' if op['r'] == 12 else f"r{op['r']}"
    elif mux == 2:
        base = f"v{ins['v']}"
    else:
        idx = f"a0 + {ins['const']}" if ins['a0x'] else str(ins['const'])
        base = f"c[{idx}]"
    swz = ''.join(SWZ[s] for s in op['swz'])
    e = base if swz == 'xyzw' else f"{base}.{swz}"
    return f"(-{e})" if op['neg'] else e


def mac_expr(ins):
    op = MAC_OPS[ins['mac']]
    A, B, C = (src_expr(ins[k], ins) for k in 'abc')
    return {
        'MOV': A, 'MUL': f"{A} * {B}", 'ADD': f"{A} + {C}", 'MAD': f"{A} * {B} + {C}",
        'DP3': f"dot({A}.xyz, {B}.xyz).xxxx", 'DPH': f"(dot({A}.xyz, {B}.xyz) + {B}.w).xxxx",
        'DP4': f"dot({A}, {B}).xxxx", 'DST': f"float4(1, {A}.y * {B}.y, {A}.z, {B}.w)",
        'MIN': f"min({A}, {B})", 'MAX': f"max({A}, {B})",
        'SLT': f"float4({A} < {B})", 'SGE': f"float4({A} >= {B})", 'ARL': A,
    }[op]


def ilu_expr(ins):
    op = ILU_OPS[ins['ilu']]
    C = src_expr(ins['c'], ins)
    return {
        'MOV': C, 'RCP': f"rcp_x({C}.x).xxxx", 'RCC': f"rcc_x({C}.x).xxxx", 'RSQ': f"rsq_x({C}.x).xxxx",
        'EXP': f"exp_x({C}.x)", 'LOG': f"log_x({C}.x)", 'LIT': f"lit_x({C})",
    }[op]


def assign(dst, mask, expr):
    m = mask_str(mask)
    if not m:
        return []
    if m == 'xyzw':
        return [f"{dst} = {expr};"]
    return [f"{dst}.{m} = ({expr}).{m};"]


def translate(code):
    """code: list of 4-DWORD instructions -> (HLSL body lines, outputs used)."""
    lines, outs = [], set()
    for words in code:
        ins = decode(words)
        mac, ilu = MAC_OPS[ins['mac']], ILU_OPS[ins['ilu']]
        if mac != 'NOP':
            lines.append(f"m = {mac_expr(ins)};")
        if ilu != 'NOP':
            lines.append(f"i = {ilu_expr(ins)};")
        if mac == 'ARL':
            lines.append("a0 = (int)floor(m.x + 0.001);")
        else:
            if mac != 'NOP':
                tgt = 'oPos' if ins['out_r'] == 12 else f"r{ins['out_r']}"
                lines += assign(tgt, ins['mac_mask'], 'm')
            if ilu != 'NOP':
                # paired with a MAC op, the ILU result goes to r1
                r = 1 if mac != 'NOP' else ins['out_r']
                tgt = 'oPos' if r == 12 else f"r{r}"
                lines += assign(tgt, ins['ilu_mask'], 'i')
        if ins['o_mask']:
            val = 'i' if ins['out_mux'] else 'm'
            if ins['orb']:
                name = OUT_REGS.get(ins['out_addr'])
                if name:
                    outs.add(name)
                    lines += assign(name, ins['o_mask'], val)
            else:
                lines += assign(f"c[{ins['out_addr']}]", ins['o_mask'], val)
        if ins['final']:
            break
    return lines, outs


HLSL_HEAD = """\
float4 c[192] : register(c0);
float4 rcp_x(float x) { return (x == 1.0f) ? 1.0f : ((x == 0.0f) ? 1.0e30f : 1.0f / x); }
float4 rcc_x(float x) { float r = rcp_x(x).x; return (r > 0) ? clamp(r, 5.42101e-20f, 1.84467e19f) : clamp(r, -1.84467e19f, -5.42101e-20f); }
float4 rsq_x(float x) { x = abs(x); return (x == 1.0f) ? 1.0f : ((x == 0.0f) ? 1.0e30f : rsqrt(x)); }
float4 exp_x(float x) { float f = floor(x); return float4(exp2(f), x - f, exp2(x), 1); }
float4 log_x(float x) { x = abs(x); if (x == 0) return float4(-1.0e30f, 1, -1.0e30f, 1);
                        float e = floor(log2(x)); return float4(e, x / exp2(e), log2(x), 1); }
float4 lit_x(float4 s) { float d = max(s.x, 0), n = max(s.y, 0), p = clamp(s.w, -127.9961f, 127.9961f);
                         return float4(1, d, (s.x > 0) ? pow(n, p) : 0, 1); }
"""

OUT_SEMANTICS = [('oPos', 'POSITION'), ('oD0', 'COLOR0'), ('oD1', 'COLOR1'), ('oFog', 'FOG'),
                 ('oPts', 'PSIZE'), ('oT0', 'TEXCOORD0'), ('oT1', 'TEXCOORD1'), ('oT2', 'TEXCOORD2'),
                 ('oT3', 'TEXCOORD3')]


def hlsl(code, inputs):
    """inputs: Xbox input registers used by the declaration."""
    body, outs = translate(code)
    outs.add('oPos')
    s = HLSL_HEAD
    scalar = ('FOG', 'PSIZE')
    s += "struct VSOUT {\n" + ''.join(f"  {'float' if sem in scalar else 'float4'} {n} : {sem};\n"
                                     for n, sem in OUT_SEMANTICS if n in outs) + "};\n"
    args = ', '.join(f"float4 v{n} : TEXCOORD{n}" for n in sorted(inputs)) or 'float4 v0 : TEXCOORD0'
    s += f"VSOUT main({args})\n{{\n"
    for n in range(16):
        if n not in inputs:
            s += f"  float4 v{n} = float4(0, 0, 0, 1);\n"
    s += "  float4 r0 = 0, r1 = 0, r2 = 0, r3 = 0, r4 = 0, r5 = 0, r6 = 0, r7 = 0, r8 = 0, r9 = 0, r10 = 0, r11 = 0;\n"
    s += "  float4 m = 0, i = 0; int a0 = 0;\n"
    s += ''.join(f"  float4 {n} = float4(0, 0, 0, 1);\n" for n, _ in OUT_SEMANTICS if n in outs)
    s += ''.join(f"  {l}\n" for l in body)
    # The Xbox shader ends in screen space (viewport scale c[58], offset c[59],
    # set by the renderer). Back to clip space for Direct3D 9:
    s += "  oPos.xyz = (oPos.xyz - c[59].xyz) / c[58].xyz;\n"
    s += "  oPos.xyz *= oPos.w;\n"
    s += "  VSOUT o;\n" + ''.join(f"  o.{n} = {n}{'.x' if sem in scalar else ''};\n"
                                   for n, sem in OUT_SEMANTICS if n in outs)
    s += "  return o;\n}\n"
    return s


# ----------------------------------------------------------- declarations --

DECL_TYPES = {  # Xbox D3DVSDT -> (bytes, D3DDECLTYPE or None)
    0x12: (4, 0), 0x22: (8, 1), 0x32: (12, 2), 0x42: (16, 3), 0x40: (4, 4),
    0x25: (4, 6), 0x45: (8, 7), 0x11: (2, None), 0x21: (4, 9), 0x31: (6, None), 0x41: (8, 10),
    0x16: (4, None), 0x15: (2, None), 0x35: (6, None), 0x14: (1, None), 0x24: (2, None),
    0x34: (3, None), 0x44: (4, 8), 0x72: (12, 2), 0x02: (0, None),
}


def parse_decl(read_u32, va):
    """-> (elements [(stream, offset, d3dtype, xbox type, reg)], constants [(reg, [4 floats]...)])"""
    elems, consts = [], []
    stream, offset = 0, 0
    while True:
        t = read_u32(va); va += 4
        if t == 0xFFFFFFFF:
            break
        kind = t >> 29
        if kind == 1:                                   # STREAM
            stream, offset = t & 0xF, 0
        elif kind == 2:                                 # STREAMDATA: REG or SKIP
            if t & 0x10000000:
                offset += ((t >> 16) & 0xF) * 4
            else:
                xt, reg = (t >> 16) & 0xFF, t & 0x1F
                size, d3d = DECL_TYPES.get(xt, (0, None))
                elems.append((stream, offset, d3d, xt, reg))
                offset += size
        elif kind == 4:                                 # CONSTMEM
            count, addr = (t >> 25) & 0xF, t & 0xFF
            vals = [read_u32(va + k * 4) for k in range(count * 4)]
            va += count * 16
            consts.append((addr, vals))
        else:
            raise SystemExit(f'[!] unsupported vertex declaration token {t:#010x}')
    return elems, consts


def read_shaders(read_u32):
    """-> [dict(fn, decl, code, elems, consts)] for the 14 table entries."""
    out = []
    for i in range(SHADER_COUNT):
        fn = read_u32(SHADER_TABLE_FN + i * 4)
        decl = read_u32(SHADER_TABLE_DECL + i * 4)
        hdr = read_u32(fn)
        n = hdr >> 16
        code = [[read_u32(fn + 4 + k * 16 + j * 4) for j in range(4)] for k in range(n)]
        elems, consts = parse_decl(read_u32, decl)
        out.append(dict(fn=fn, decl=decl, header=hdr, code=code, elems=elems, consts=consts))
    return out


def c_string(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"').replace('\n', '\\n"\n    "') + '"'


def emit_c(read_u32, path):
    """Generated C table for src/PC (build/pc/vshaders.inc): per shader the
    microcode address, HLSL source, D3DVERTEXELEMENT9 list and declaration
    constants (hardware register, 4 DWORDs)."""
    shaders = read_shaders(read_u32)
    out = ['/* generated by tools/nv2a_vsh.py from default.xbe - do not edit */',
           f'#define VSH_COUNT {len(shaders)}']
    out.append('static const u32 g_vsh_fn[VSH_COUNT] = { ' + ', '.join(f"{sh['fn']:#x}" for sh in shaders) + ' };')
    out.append('static const char* const g_vsh_src[VSH_COUNT] = {')
    for sh in shaders:
        out.append('    ' + c_string(hlsl(sh['code'], {e[4] for e in sh['elems']})) + ',')
    out.append('};')
    out.append('/* D3DVERTEXELEMENT9: Stream, Offset, Type, Method, Usage (TEXCOORD = 5), UsageIndex = Xbox v# */')
    out.append('static const u16 g_vsh_decl[VSH_COUNT][17][4] = {')
    for sh in shaders:
        els = []
        for stream, off, d3d, xt, reg in sh['elems']:
            if d3d is None:
                raise SystemExit(f"[!] shader at {sh['fn']:#x}: Xbox vertex type {xt:#x} has no Direct3D 9 twin")
            els.append(f"{{ {stream}, {off}, {d3d} | (0 << 8), 5 | ({reg} << 8) }}")
        els.append("{ 0xFF, 0, 17, 0 }")
        out.append('    { ' + ', '.join(els) + ' },')
    out.append('};')
    out.append('static const u32 g_vsh_consts[][6] = {   /* shader, register, 4 DWORDs */')
    for i, sh in enumerate(shaders):
        for addr, vals in sh['consts']:
            for k in range(len(vals) // 4):
                out.append(f"    {{ {i}, {addr + k}, " + ', '.join(f"{v:#x}" for v in vals[k * 4:k * 4 + 4]) + ' },')
    out.append('    { 0xFFFFFFFF }')
    out.append('};')
    open(path, 'w').write('\n'.join(out) + '\n')


def xbe_reader(path):
    x = open(path, 'rb').read()
    base = struct.unpack_from('<I', x, 0x104)[0]
    nsec, shdr = struct.unpack_from('<II', x, 0x11C)
    secs = [struct.unpack_from('<IIIIII', x, shdr - base + i * 56) for i in range(nsec)]

    def read_u32(va):
        for _, v, vs, ra, rs, _ in secs:
            if v <= va < v + vs:
                return struct.unpack_from('<I', x, ra + va - v)[0] if va - v < rs else 0
        raise SystemExit(f'[!] address {va:#x} not in the image')
    return read_u32


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else 'rom/extracted/default.xbe'
    for i, sh in enumerate(read_shaders(xbe_reader(path))):
        print(f"==== shader {i}: fn {sh['fn']:#x} decl {sh['decl']:#x} header {sh['header']:#010x} "
              f"({len(sh['code'])} instructions)")
        for e in sh['elems']:
            print(f"  stream {e[0]} +{e[1]:<3} v{e[4]:<2} xbox type {e[3]:#04x} -> d3d9 {e[2]}")
        for addr, vals in sh['consts']:
            print(f"  const c{addr}: {[hex(v) for v in vals]}")
        for k, w in enumerate(sh['code']):
            ins = decode(w)
            print(f"  {k:3}: {MAC_OPS[ins['mac']]:4} {ILU_OPS[ins['ilu']]:4} out_r={ins['out_r']} "
                  f"mac={mask_str(ins['mac_mask'])} ilu={mask_str(ins['ilu_mask'])} "
                  f"o={OUT_REGS.get(ins['out_addr'], ins['out_addr']) if ins['orb'] else 'c' + str(ins['out_addr'])}."
                  f"{mask_str(ins['o_mask'])} c={ins['const']} v={ins['v']}{' FINAL' if ins['final'] else ''}")
        print(hlsl(sh['code'], {e[4] for e in sh['elems']}))


if __name__ == '__main__':
    main()
