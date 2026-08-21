#!/usr/bin/env python3
"""Emit SPIR-V for the headless PS1 triangle graphics pipeline.

No glslang. Constants are emitted before functions (SPIR-V layout rule).
The fragment shader replicates gpu_render_triangle coverage, shading,
4x4 dither, BGR555, and semi-transparency.
"""
from __future__ import annotations

import struct
from pathlib import Path

OP = {
    "Capability": 17,
    "MemoryModel": 14,
    "EntryPoint": 15,
    "ExecutionMode": 16,
    "Decorate": 71,
    "MemberDecorate": 72,
    "TypeVoid": 19,
    "TypeBool": 20,
    "TypeInt": 21,
    "TypeFloat": 22,
    "TypeVector": 23,
    "TypeImage": 25,
    "TypeSampledImage": 27,
    "TypeStruct": 30,
    "TypePointer": 32,
    "TypeFunction": 33,
    "Constant": 43,
    "Function": 54,
    "FunctionEnd": 56,
    "Variable": 59,
    "Load": 61,
    "Store": 62,
    "AccessChain": 65,
    "CompositeConstruct": 80,
    "CompositeExtract": 81,
    "Image": 100,
    "ImageFetch": 95,
    "ConvertFToU": 109,
    "ConvertFToS": 110,
    "ConvertSToF": 111,
    "ConvertUToF": 112,
    "IAdd": 128,
    "FAdd": 129,
    "ISub": 130,
    "FSub": 131,
    "IMul": 132,
    "FMul": 133,
    "FDiv": 136,
    "BitwiseOr": 197,
    "BitwiseAnd": 199,
    "ShiftLeftLogical": 196,
    "ShiftRightLogical": 194,
    "LogicalAnd": 167,
    "LogicalOr": 166,
    "LogicalNot": 168,
    "Select": 169,
    "IEqual": 170,
    "INotEqual": 171,
    "SGreaterThan": 173,
    "SLessThan": 177,
    "SLessThanEqual": 179,
    "FOrdEqual": 180,
    "FOrdLessThan": 184,
    "FOrdGreaterThan": 186,
    "Phi": 245,
    "SelectionMerge": 247,
    "Label": 248,
    "Branch": 249,
    "BranchConditional": 250,
    "Return": 253,
}

CAP_SHADER = 1
MODEL_LOGICAL = 0
GLSL450 = 1
EXEC_VERTEX = 0
EXEC_FRAGMENT = 4
MODE_ORIGIN_UPPER_LEFT = 7
SC_UNIFORM_CONSTANT = 0
SC_INPUT = 1
SC_OUTPUT = 3
SC_FUNCTION = 7
SC_PUSH_CONSTANT = 9
DEC_BLOCK = 2
DEC_BUILTIN = 11
DEC_LOCATION = 30
DEC_BINDING = 33
DEC_DESCRIPTOR_SET = 34
DEC_OFFSET = 35
BUILTIN_POSITION = 0
BUILTIN_FRAGCOORD = 15
BUILTIN_VERTEX_INDEX = 42
IMG_DIM_2D = 1
SAMPLED_YES = 1
FMT_UNKNOWN = 0
FN_NONE = 0
LOD = 2


def u32(n: int) -> int:
    return n & 0xFFFFFFFF


def fbits(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


def str_words(text: str) -> list[int]:
    raw = text.encode("utf-8") + b"\x00"
    while len(raw) % 4:
        raw += b"\x00"
    return [int.from_bytes(raw[i : i + 4], "little") for i in range(0, len(raw), 4)]


class Spv:
    def __init__(self) -> None:
        self.words: list[int] = []
        self.next_id = 1

    def id(self) -> int:
        i = self.next_id
        self.next_id += 1
        return i

    def emit(self, op: str, *operands: int) -> None:
        opcode = OP[op]
        payload = [u32(x) for x in operands]
        self.words.append((len(payload) + 1) << 16 | opcode)
        self.words.extend(payload)

    def finish(self, version: int = 0x00010000) -> bytes:
        header = [0x07230203, version, 0, self.next_id, 0]
        words = header + self.words
        return struct.pack("<" + "I" * len(words), *words)


def emit_vert() -> bytes:
    s = Spv()
    s.emit("Capability", CAP_SHADER)
    s.emit("MemoryModel", MODEL_LOGICAL, GLSL450)
    main = s.id()
    vid = s.id()
    pos = s.id()
    s.emit("EntryPoint", EXEC_VERTEX, main, *str_words("main"), vid, pos)

    t_void = s.id(); s.emit("TypeVoid", t_void)
    t_fn = s.id(); s.emit("TypeFunction", t_fn, t_void)
    t_int = s.id(); s.emit("TypeInt", t_int, 32, 1)
    t_float = s.id(); s.emit("TypeFloat", t_float, 32)
    t_v2 = s.id(); s.emit("TypeVector", t_v2, t_float, 2)
    t_v4 = s.id(); s.emit("TypeVector", t_v4, t_float, 4)
    p_in_int = s.id(); s.emit("TypePointer", p_in_int, SC_INPUT, t_int)
    p_out_v4 = s.id(); s.emit("TypePointer", p_out_v4, SC_OUTPUT, t_v4)

    def cint(v: int) -> int:
        i = s.id(); s.emit("Constant", t_int, i, u32(v)); return i

    def cfloat(v: float) -> int:
        i = s.id(); s.emit("Constant", t_float, i, fbits(v)); return i

    c1 = cint(1)
    c2 = cint(2)
    cf0 = cfloat(0.0)
    cf1 = cfloat(1.0)
    cf2 = cfloat(2.0)

    s.emit("Decorate", vid, DEC_BUILTIN, BUILTIN_VERTEX_INDEX)
    s.emit("Decorate", pos, DEC_BUILTIN, BUILTIN_POSITION)
    s.emit("Variable", p_in_int, vid, SC_INPUT)
    s.emit("Variable", p_out_v4, pos, SC_OUTPUT)

    s.emit("Function", t_void, main, FN_NONE, t_fn)
    lab = s.id(); s.emit("Label", lab)
    iv = s.id(); s.emit("Load", t_int, iv, vid)
    sl = s.id(); s.emit("ShiftLeftLogical", t_int, sl, iv, c1)
    ax = s.id(); s.emit("BitwiseAnd", t_int, ax, sl, c2)
    ay = s.id(); s.emit("BitwiseAnd", t_int, ay, iv, c2)
    fx = s.id(); s.emit("ConvertSToF", t_float, fx, ax)
    fy = s.id(); s.emit("ConvertSToF", t_float, fy, ay)
    uv = s.id(); s.emit("CompositeConstruct", t_v2, uv, fx, fy)
    two = s.id(); s.emit("CompositeConstruct", t_v2, two, cf2, cf2)
    one = s.id(); s.emit("CompositeConstruct", t_v2, one, cf1, cf1)
    scaled = s.id(); s.emit("FMul", t_v2, scaled, uv, two)
    ndc = s.id(); s.emit("FSub", t_v2, ndc, scaled, one)
    nx = s.id(); s.emit("CompositeExtract", t_float, nx, ndc, 0)
    ny = s.id(); s.emit("CompositeExtract", t_float, ny, ndc, 1)
    outv = s.id(); s.emit("CompositeConstruct", t_v4, outv, nx, ny, cf0, cf1)
    s.emit("Store", pos, outv)
    s.emit("Return")
    s.emit("FunctionEnd")
    return s.finish()


def emit_frag() -> bytes:
    s = Spv()
    s.emit("Capability", CAP_SHADER)
    s.emit("MemoryModel", MODEL_LOGICAL, GLSL450)
    main = s.id()
    fragcoord = s.id()
    out_color = s.id()
    src_samp = s.id()
    pc_var = s.id()
    s.emit(
        "EntryPoint",
        EXEC_FRAGMENT,
        main,
        *str_words("main"),
        fragcoord,
        out_color,
        src_samp,
        pc_var,
    )
    s.emit("ExecutionMode", main, MODE_ORIGIN_UPPER_LEFT)

    t_void = s.id(); s.emit("TypeVoid", t_void)
    t_fn = s.id(); s.emit("TypeFunction", t_fn, t_void)
    t_bool = s.id(); s.emit("TypeBool", t_bool)
    t_int = s.id(); s.emit("TypeInt", t_int, 32, 1)
    t_uint = s.id(); s.emit("TypeInt", t_uint, 32, 0)
    t_float = s.id(); s.emit("TypeFloat", t_float, 32)
    t_v4f = s.id(); s.emit("TypeVector", t_v4f, t_float, 4)
    t_v2i = s.id(); s.emit("TypeVector", t_v2i, t_int, 2)
    t_v4u = s.id(); s.emit("TypeVector", t_v4u, t_uint, 4)
    t_img = s.id()
    s.emit("TypeImage", t_img, t_uint, IMG_DIM_2D, 0, 0, 0, SAMPLED_YES, FMT_UNKNOWN)
    t_sampled = s.id(); s.emit("TypeSampledImage", t_sampled, t_img)

    members = [t_int] * 6 + [t_uint] * 5 + [t_int] * 9
    t_pc = s.id(); s.emit("TypeStruct", t_pc, *members)
    s.emit("Decorate", t_pc, DEC_BLOCK)
    for i in range(20):
        s.emit("MemberDecorate", t_pc, i, DEC_OFFSET, i * 4)
    s.emit("Decorate", fragcoord, DEC_BUILTIN, BUILTIN_FRAGCOORD)
    s.emit("Decorate", out_color, DEC_LOCATION, 0)
    s.emit("Decorate", src_samp, DEC_DESCRIPTOR_SET, 0)
    s.emit("Decorate", src_samp, DEC_BINDING, 0)

    p_in_v4 = s.id(); s.emit("TypePointer", p_in_v4, SC_INPUT, t_v4f)
    p_out_u = s.id(); s.emit("TypePointer", p_out_u, SC_OUTPUT, t_uint)
    p_uc_samp = s.id(); s.emit("TypePointer", p_uc_samp, SC_UNIFORM_CONSTANT, t_sampled)
    p_pc = s.id(); s.emit("TypePointer", p_pc, SC_PUSH_CONSTANT, t_pc)
    p_pc_int = s.id(); s.emit("TypePointer", p_pc_int, SC_PUSH_CONSTANT, t_int)
    p_pc_uint = s.id(); s.emit("TypePointer", p_pc_uint, SC_PUSH_CONSTANT, t_uint)
    p_fn_uint = s.id(); s.emit("TypePointer", p_fn_uint, SC_FUNCTION, t_uint)

    int_ids: dict[int, int] = {}
    uint_ids: dict[int, int] = {}
    float_ids: dict[float, int] = {}

    def cint(v: int) -> int:
        v = int(v)
        if v not in int_ids:
            i = s.id()
            s.emit("Constant", t_int, i, u32(v))
            int_ids[v] = i
        return int_ids[v]

    def cuint(v: int) -> int:
        v = int(v) & 0xFFFFFFFF
        if v not in uint_ids:
            i = s.id()
            s.emit("Constant", t_uint, i, v)
            uint_ids[v] = i
        return uint_ids[v]

    def cfloat(v: float) -> int:
        if v not in float_ids:
            i = s.id()
            s.emit("Constant", t_float, i, fbits(v))
            float_ids[v] = i
        return float_ids[v]

    for n in range(20):
        cint(n)
    for n in (-4, -3, -2, -1, 0, 1, 2, 3, 4, 255):
        cint(n)
    for n in (0, 1, 2, 3, 5, 6, 8, 9, 10, 16, 0x1F, 0xF8, 0xFF, 0x00F800, 0xF80000):
        cuint(n)
    for n in (0.0, 0.25, 0.5, 1.0, 255.0):
        cfloat(n)

    kernel = [-4, 0, -3, 1, 2, -2, 3, -1, -3, 1, -4, 0, 3, -1, 2, -2]
    k_ids = [cint(v) for v in kernel]

    s.emit("Variable", p_in_v4, fragcoord, SC_INPUT)
    s.emit("Variable", p_out_u, out_color, SC_OUTPUT)
    s.emit("Variable", p_uc_samp, src_samp, SC_UNIFORM_CONSTANT)
    s.emit("Variable", p_pc, pc_var, SC_PUSH_CONSTANT)

    s.emit("Function", t_void, main, FN_NONE, t_fn)
    entry = s.id(); s.emit("Label", entry)
    v_result = s.id(); s.emit("Variable", p_fn_uint, v_result, SC_FUNCTION)

    def load_pc_int(member: int) -> int:
        ptr = s.id()
        s.emit("AccessChain", p_pc_int, ptr, pc_var, cint(member))
        val = s.id()
        s.emit("Load", t_int, val, ptr)
        return val

    def load_pc_uint(member: int) -> int:
        ptr = s.id()
        s.emit("AccessChain", p_pc_uint, ptr, pc_var, cint(member))
        val = s.id()
        s.emit("Load", t_uint, val, ptr)
        return val

    ax, ay = load_pc_int(0), load_pc_int(1)
    bx, by = load_pc_int(2), load_pc_int(3)
    cx, cy = load_pc_int(4), load_pc_int(5)
    col0, col1, col2 = load_pc_uint(6), load_pc_uint(7), load_pc_uint(8)
    mod0 = load_pc_uint(9)
    attrib = load_pc_uint(10)
    xmin, ymin = load_pc_int(11), load_pc_int(12)
    xmax, ymax = load_pc_int(13), load_pc_int(14)
    dx1, dy1 = load_pc_int(15), load_pc_int(16)
    dx2, dy2 = load_pc_int(17), load_pc_int(18)
    tmode = load_pc_int(19)

    fc = s.id(); s.emit("Load", t_v4f, fc, fragcoord)
    fx = s.id(); s.emit("CompositeExtract", t_float, fx, fc, 0)
    fy = s.id(); s.emit("CompositeExtract", t_float, fy, fc, 1)
    ix = s.id(); s.emit("ConvertFToS", t_int, ix, fx)
    iy = s.id(); s.emit("ConvertFToS", t_int, iy, fy)
    coord = s.id(); s.emit("CompositeConstruct", t_v2i, coord, ix, iy)
    samp = s.id(); s.emit("Load", t_sampled, samp, src_samp)
    img = s.id(); s.emit("Image", t_img, img, samp)
    fetched = s.id()
    s.emit("ImageFetch", t_v4u, fetched, img, coord, LOD, cint(0))
    back = s.id(); s.emit("CompositeExtract", t_uint, back, fetched, 0)
    s.emit("Store", v_result, back)

    def slt(a: int, b: int) -> int:
        r = s.id(); s.emit("SLessThan", t_bool, r, a, b); return r

    def sle(a: int, b: int) -> int:
        r = s.id(); s.emit("SLessThanEqual", t_bool, r, a, b); return r

    def sgt(a: int, b: int) -> int:
        r = s.id(); s.emit("SGreaterThan", t_bool, r, a, b); return r

    def land(a: int, b: int) -> int:
        r = s.id(); s.emit("LogicalAnd", t_bool, r, a, b); return r

    def lor(a: int, b: int) -> int:
        r = s.id(); s.emit("LogicalOr", t_bool, r, a, b); return r

    def lnot(a: int) -> int:
        r = s.id(); s.emit("LogicalNot", t_bool, r, a); return r

    def ieq(a: int, b: int) -> int:
        r = s.id(); s.emit("IEqual", t_bool, r, a, b); return r

    in_box = land(land(land(sle(xmin, ix), slt(ix, xmax)), sle(ymin, iy)), slt(iy, ymax))
    in_draw = land(land(land(sle(dx1, ix), sle(ix, dx2)), sle(dy1, iy)), sle(iy, dy2))
    maybe = land(in_box, in_draw)

    merge_cov = s.id()
    in_cov = s.id()
    s.emit("SelectionMerge", merge_cov, 0)
    s.emit("BranchConditional", maybe, in_cov, merge_cov)

    s.emit("Label", in_cov)

    def tof(i: int) -> int:
        r = s.id(); s.emit("ConvertSToF", t_float, r, i); return r

    fax, fay, fbx, fby, fcx, fcy = map(tof, (ax, ay, bx, by, cx, cy))
    fpx, fpy = map(tof, (ix, iy))

    def edge(ax_, ay_, bx_, by_, cx_, cy_) -> int:
        t0 = s.id(); s.emit("FSub", t_float, t0, bx_, ax_)
        t1 = s.id(); s.emit("FSub", t_float, t1, cy_, ay_)
        t2 = s.id(); s.emit("FMul", t_float, t2, t0, t1)
        t3 = s.id(); s.emit("FSub", t_float, t3, by_, ay_)
        t4 = s.id(); s.emit("FSub", t_float, t4, cx_, ax_)
        t5 = s.id(); s.emit("FMul", t_float, t5, t3, t4)
        t6 = s.id(); s.emit("FSub", t_float, t6, t2, t5)
        return t6

    z0 = edge(fbx, fby, fcx, fcy, fpx, fpy)
    z1 = edge(fcx, fcy, fax, fay, fpx, fpy)
    z2 = edge(fax, fay, fbx, fby, fpx, fpy)
    area = edge(fax, fay, fbx, fby, fcx, fcy)

    def tl(z: int, ay_: int, by_: int, ax_: int, bx_: int) -> int:
        zneg = s.id(); s.emit("FOrdLessThan", t_bool, zneg, z, cfloat(0.0))
        zeq = s.id(); s.emit("FOrdEqual", t_bool, zeq, z, cfloat(0.0))
        bygt = sgt(by_, ay_)
        byeq = ieq(by_, ay_)
        bxlt = slt(bx_, ax_)
        on = land(zeq, lor(bygt, land(byeq, bxlt)))
        return lor(zneg, on)

    skip = lor(lor(tl(z0, by, cy, bx, cx), tl(z1, cy, ay, cx, ax)), tl(z2, ay, by, ax, bx))
    area_zero = s.id(); s.emit("FOrdEqual", t_bool, area_zero, area, cfloat(0.0))
    inside = land(lnot(skip), lnot(area_zero))

    merge_in = s.id()
    do_shade = s.id()
    s.emit("SelectionMerge", merge_in, 0)
    s.emit("BranchConditional", inside, do_shade, merge_in)

    s.emit("Label", do_shade)
    masked = s.id(); s.emit("BitwiseAnd", t_uint, masked, attrib, cuint(0x10))
    is_shaded = s.id(); s.emit("INotEqual", t_bool, is_shaded, masked, cuint(0))

    merge_sh = s.id()
    sh_yes = s.id()
    sh_no = s.id()
    s.emit("SelectionMerge", merge_sh, 0)
    s.emit("BranchConditional", is_shaded, sh_yes, sh_no)

    def ch(col: int, shift_id: int) -> int:
        shv = s.id(); s.emit("ShiftRightLogical", t_uint, shv, col, shift_id)
        band = s.id(); s.emit("BitwiseAnd", t_uint, band, shv, cuint(0xFF))
        fl = s.id(); s.emit("ConvertUToF", t_float, fl, band)
        return fl

    def interp(shift_id: int) -> int:
        a, b, c = ch(col0, shift_id), ch(col1, shift_id), ch(col2, shift_id)
        t0 = s.id(); s.emit("FMul", t_float, t0, z0, a)
        t1 = s.id(); s.emit("FMul", t_float, t1, z1, b)
        t2 = s.id(); s.emit("FMul", t_float, t2, z2, c)
        s01 = s.id(); s.emit("FAdd", t_float, s01, t0, t1)
        s012 = s.id(); s.emit("FAdd", t_float, s012, s01, t2)
        d = s.id(); s.emit("FDiv", t_float, d, s012, area)
        return d

    def clampf(v: int) -> int:
        lo = s.id(); s.emit("FOrdLessThan", t_bool, lo, v, cfloat(0.0))
        hi = s.id(); s.emit("FOrdGreaterThan", t_bool, hi, v, cfloat(255.0))
        t0 = s.id(); s.emit("Select", t_float, t0, lo, cfloat(0.0), v)
        t1 = s.id(); s.emit("Select", t_float, t1, hi, cfloat(255.0), t0)
        return t1

    def uround(v: int) -> int:
        add = s.id(); s.emit("FAdd", t_float, add, v, cfloat(0.5))
        u = s.id(); s.emit("ConvertFToU", t_uint, u, add)
        return u

    def fadd(a: int, b: int) -> int:
        r = s.id(); s.emit("FAdd", t_float, r, a, b); return r

    s.emit("Label", sh_yes)
    cr, cg, cb = interp(cuint(0)), interp(cuint(8)), interp(cuint(16))
    dx = s.id(); s.emit("ISub", t_int, dx, ix, xmin)
    dy = s.id(); s.emit("ISub", t_int, dy, iy, ymin)
    dxm = s.id(); s.emit("BitwiseAnd", t_int, dxm, dx, cint(3))
    dym = s.id(); s.emit("BitwiseAnd", t_int, dym, dy, cint(3))
    y4 = s.id(); s.emit("IMul", t_int, y4, dym, cint(4))
    didx = s.id(); s.emit("IAdd", t_int, didx, dxm, y4)
    dval = k_ids[-1]
    for i, kid in enumerate(k_ids[:-1]):
        eq = ieq(didx, cint(i))
        sel = s.id(); s.emit("Select", t_int, sel, eq, kid, dval)
        dval = sel
    df = s.id(); s.emit("ConvertSToF", t_float, df, dval)
    cr, cg, cb = clampf(fadd(cr, df)), clampf(fadd(cg, df)), clampf(fadd(cb, df))
    ucr, ucg, ucb = uround(cr), uround(cg), uround(cb)
    gsh = s.id(); s.emit("ShiftLeftLogical", t_uint, gsh, ucg, cuint(8))
    bsh = s.id(); s.emit("ShiftLeftLogical", t_uint, bsh, ucb, cuint(16))
    rg = s.id(); s.emit("BitwiseOr", t_uint, rg, ucr, gsh)
    rgb_s = s.id(); s.emit("BitwiseOr", t_uint, rgb_s, rg, bsh)
    s.emit("Branch", merge_sh)

    s.emit("Label", sh_no)
    s.emit("Branch", merge_sh)

    s.emit("Label", merge_sh)
    mod = s.id()
    s.emit("Phi", t_uint, mod, rgb_s, sh_yes, mod0, sh_no)

    def bgr555(rgb: int) -> int:
        r = s.id(); s.emit("BitwiseAnd", t_uint, r, rgb, cuint(0xF8))
        r2 = s.id(); s.emit("ShiftRightLogical", t_uint, r2, r, cuint(3))
        g = s.id(); s.emit("BitwiseAnd", t_uint, g, rgb, cuint(0x00F800))
        g2 = s.id(); s.emit("ShiftRightLogical", t_uint, g2, g, cuint(6))
        b = s.id(); s.emit("BitwiseAnd", t_uint, b, rgb, cuint(0xF80000))
        b2 = s.id(); s.emit("ShiftRightLogical", t_uint, b2, b, cuint(9))
        rg2 = s.id(); s.emit("BitwiseOr", t_uint, rg2, r2, g2)
        out = s.id(); s.emit("BitwiseOr", t_uint, out, rg2, b2)
        return out

    color = bgr555(mod)
    trmask = s.id(); s.emit("BitwiseAnd", t_uint, trmask, attrib, cuint(2))
    is_tr = s.id(); s.emit("INotEqual", t_bool, is_tr, trmask, cuint(0))
    merge_tr = s.id()
    tr_yes = s.id()
    s.emit("SelectionMerge", merge_tr, 0)
    s.emit("BranchConditional", is_tr, tr_yes, merge_tr)

    s.emit("Label", tr_yes)

    def expand5(src: int, shift_id: int) -> int:
        shv = s.id(); s.emit("ShiftRightLogical", t_uint, shv, src, shift_id)
        band = s.id(); s.emit("BitwiseAnd", t_uint, band, shv, cuint(0x1F))
        sl = s.id(); s.emit("ShiftLeftLogical", t_uint, sl, band, cuint(3))
        fl = s.id(); s.emit("ConvertUToF", t_float, fl, sl)
        return fl

    def sel3(cond: int, a: int, b: int) -> int:
        r = s.id(); s.emit("Select", t_float, r, cond, a, b); return r

    crf, cgf, cbf = expand5(color, cuint(0)), expand5(color, cuint(5)), expand5(color, cuint(10))
    brf, bgf, bbf = expand5(back, cuint(0)), expand5(back, cuint(5)), expand5(back, cuint(10))
    is0, is1, is2 = ieq(tmode, cint(0)), ieq(tmode, cint(1)), ieq(tmode, cint(2))

    def mix_chan(bf: int, cf: int) -> int:
        avg_b = s.id(); s.emit("FMul", t_float, avg_b, bf, cfloat(0.5))
        avg_c = s.id(); s.emit("FMul", t_float, avg_c, cf, cfloat(0.5))
        avg = s.id(); s.emit("FAdd", t_float, avg, avg_b, avg_c)
        add = s.id(); s.emit("FAdd", t_float, add, bf, cf)
        sub = s.id(); s.emit("FSub", t_float, sub, bf, cf)
        q = s.id(); s.emit("FMul", t_float, q, cf, cfloat(0.25))
        addq = s.id(); s.emit("FAdd", t_float, addq, bf, q)
        t = sel3(is2, sub, addq)
        t = sel3(is1, add, t)
        t = sel3(is0, avg, t)
        return clampf(t)

    br, bg, bb = mix_chan(brf, crf), mix_chan(bgf, cgf), mix_chan(bbf, cbf)
    ur, ug, ub = uround(br), uround(bg), uround(bb)
    gsh2 = s.id(); s.emit("ShiftLeftLogical", t_uint, gsh2, ug, cuint(8))
    bsh2 = s.id(); s.emit("ShiftLeftLogical", t_uint, bsh2, ub, cuint(16))
    rg3 = s.id(); s.emit("BitwiseOr", t_uint, rg3, ur, gsh2)
    rgb_t = s.id(); s.emit("BitwiseOr", t_uint, rgb_t, rg3, bsh2)
    color_t = bgr555(rgb_t)
    s.emit("Branch", merge_tr)

    s.emit("Label", merge_tr)
    color_final = s.id()
    s.emit("Phi", t_uint, color_final, color_t, tr_yes, color, merge_sh)
    s.emit("Store", v_result, color_final)
    s.emit("Branch", merge_in)

    s.emit("Label", merge_in)
    s.emit("Branch", merge_cov)

    s.emit("Label", merge_cov)
    loaded = s.id(); s.emit("Load", t_uint, loaded, v_result)
    s.emit("Store", out_color, loaded)
    s.emit("Return")
    s.emit("FunctionEnd")
    return s.finish()


def as_c_array(name: str, blob: bytes) -> str:
    words = [int.from_bytes(blob[i : i + 4], "little") for i in range(0, len(blob), 4)]
    lines = [f"static const uint32_t {name}[] = {{"]
    row: list[str] = []
    for w in words:
        row.append(f"0x{w:08x}u")
        if len(row) == 6:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("    " + ", ".join(row) + ",")
    lines.append("};")
    lines.append(f"static const size_t {name}_words = {len(words)};")
    return "\n".join(lines) + "\n"


def main() -> None:
    here = Path(__file__).resolve().parent
    vert = emit_vert()
    frag = emit_frag()
    text = (
        "/* Generated by vk/shaders/gen_spv.py — do not hand-edit. */\n"
        "#include <stddef.h>\n#include <stdint.h>\n\n"
        + as_c_array("vk_tri_vert_spv", vert)
        + "\n"
        + as_c_array("vk_tri_frag_spv", frag)
    )
    (here / "tri_shaders.inc").write_text(text)
    print(f"wrote tri_shaders.inc vert={len(vert)} frag={len(frag)}")


if __name__ == "__main__":
    main()
