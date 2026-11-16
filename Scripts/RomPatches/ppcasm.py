"""
A small PowerPC assembler for ROM patches: the instructions a patch's routines
use (integer, load/store, compare, branch, single-precision float), labels,
.word/.float/.space/.align data, operands as Python expressions over the
labels and symbols given (ha(), lo(), hi() for lis/addi pairs).

    words = assemble('''
        lis   r9, ha(table)
        lwz   r3, lo(table)(r9)
        blr
    ''', origin=0x0F2600, symbols={'table': 0x13EE00})

Returns {address: word}. Big-endian words, as the PowerPC reads them.
"""

import re
import struct

__all__ = ["assemble", "ha", "lo", "hi", "AsmError"]


class AsmError(Exception):
    pass


def lo(v):
    return v & 0xFFFF


def hi(v):
    return (v >> 16) & 0xFFFF


def ha(v):
    """The high half for a lis/addi pair (addi sign-extends the low half)."""
    return ((v + 0x8000) >> 16) & 0xFFFF


def _float_bits(x):
    return struct.unpack(">I", struct.pack(">f", x))[0]


# --- Operand parsing

_REG = re.compile(r"^(r|f|cr)(\d+)$", re.I)


def _reg(text, kind):
    m = _REG.match(text.strip())
    if not m or m.group(1).lower() != kind:
        raise AsmError("expected a %s register, got '%s'" % (kind, text))
    n = int(m.group(2))
    if n > (7 if kind == "cr" else 31):
        raise AsmError("no register %s" % text)
    return n


def _split_operands(text):
    out, depth, cur = [], 0, ""
    for ch in text:
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
            continue
        depth += ch == "("
        depth -= ch == ")"
        cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


class _Ctx:
    def __init__(self, symbols, final):
        self.symbols = symbols
        self.final = final

    def value(self, text):
        env = {"ha": ha, "lo": lo, "hi": hi, "float": _float_bits}
        env.update(self.symbols)
        try:
            return int(eval(text, {"__builtins__": {}}, env))
        except NameError:
            if self.final:
                raise AsmError("unknown symbol in '%s'" % text)
            return 0

    def mem(self, text):
        """d(rA)"""
        m = re.match(r"^(.*)\((r\d+)\)$", text.strip(), re.I)
        if not m:
            raise AsmError("expected d(rA), got '%s'" % text)
        d = self.value(m.group(1)) if m.group(1).strip() else 0
        return d, _reg(m.group(2), "r")


def _simm(v, what):
    if v > 0xFFFF or v < -0x8000:
        raise AsmError("%s 0x%X out of 16 bits" % (what, v))
    return v & 0xFFFF


def _uimm(v, what):
    if v < 0 or v > 0xFFFF:
        raise AsmError("%s 0x%X out of 16 bits (unsigned)" % (what, v))
    return v


# --- Instruction forms

_D_ARITH = {"addi": 14, "addis": 15, "mulli": 7, "subfic": 8}
_D_LOGIC = {"ori": 24, "oris": 25, "xori": 26, "xoris": 27, "andi.": 28, "andis.": 29}
_D_MEM = {"lwz": 32, "lwzu": 33, "lbz": 34, "lbzu": 35, "stw": 36, "stwu": 37, "stb": 38, "stbu": 39,
          "lhz": 40, "lhzu": 41, "lha": 42, "sth": 44, "sthu": 45}
_D_FMEM = {"lfs": 48, "lfsu": 49, "lfd": 50, "stfs": 52, "stfsu": 53, "stfd": 54}
# X-form, opcode 31: rD, rA, rB (arithmetic, XO << 1)
_X_ARITH = {"add": 266, "subf": 40, "mullw": 235, "divw": 491, "divwu": 459, "mulhw": 75, "mulhwu": 11}
# X-form, opcode 31: rA, rS, rB (logical, the result in rA)
_X_LOGIC = {"and": 28, "or": 444, "xor": 316, "nor": 124, "andc": 60, "slw": 24, "srw": 536, "sraw": 792}
_X_MEMX = {"lwzx": 23, "stwx": 151, "lbzx": 87, "stbx": 215, "lhzx": 279, "sthx": 407, "lfsx": 535, "stfsx": 663,
           "lwbrx": 534, "stwbrx": 662}
# Float, opcode 59 (single) and 63 (double): frD, frA, frB
_F_AB = {"fadds": (59, 21), "fsubs": (59, 20), "fdivs": (59, 18), "fadd": (63, 21), "fsub": (63, 20), "fdiv": (63, 18)}
_F_AC = {"fmuls": (59, 25), "fmul": (63, 25)}
_F_B = {"fmr": 72, "fneg": 40, "fabs": 264, "frsp": 12, "fctiwz": 15}
# Conditional branches: BO, bit in the CR field
_BC = {"blt": (12, 0), "bgt": (12, 1), "beq": (12, 2), "bge": (4, 0), "ble": (4, 1), "bne": (4, 2)}
_SPR = {"lr": 8, "ctr": 9}


def _spr_field(n):
    return ((n & 0x1F) << 5 | (n >> 5)) << 11


def _encode(op, args, pc, ctx):
    a = args
    if op == "nop":
        return 0x60000000
    if op == "blr":
        return 0x4E800020
    if op == "blrl":
        return 0x4E800021
    if op == "bctr":
        return 0x4E800420
    if op == "bctrl":
        return 0x4E800421
    if op == "li":
        return 14 << 26 | _reg(a[0], "r") << 21 | _simm(ctx.value(a[1]), op)
    if op == "lis":
        return 15 << 26 | _reg(a[0], "r") << 21 | (ctx.value(a[1]) & 0xFFFF)
    if op == "mr":
        s = _reg(a[1], "r")
        return 31 << 26 | s << 21 | _reg(a[0], "r") << 16 | s << 11 | 444 << 1
    if op in _D_ARITH:
        imm = ctx.value(a[2])
        imm = (imm & 0xFFFF) if op == "addis" else _simm(imm, op)
        return _D_ARITH[op] << 26 | _reg(a[0], "r") << 21 | _reg(a[1], "r") << 16 | imm
    if op == "subi":
        return 14 << 26 | _reg(a[0], "r") << 21 | _reg(a[1], "r") << 16 | _simm(-ctx.value(a[2]), op)
    if op in _D_LOGIC:
        return _D_LOGIC[op] << 26 | _reg(a[1], "r") << 21 | _reg(a[0], "r") << 16 | _uimm(ctx.value(a[2]) & 0xFFFF, op)
    if op in _D_MEM:
        d, ra = ctx.mem(a[1])
        return _D_MEM[op] << 26 | _reg(a[0], "r") << 21 | ra << 16 | _simm(d, op)
    if op in _D_FMEM:
        d, ra = ctx.mem(a[1])
        return _D_FMEM[op] << 26 | _reg(a[0], "f") << 21 | ra << 16 | _simm(d, op)
    if op in _X_ARITH:
        return 31 << 26 | _reg(a[0], "r") << 21 | _reg(a[1], "r") << 16 | _reg(a[2], "r") << 11 | _X_ARITH[op] << 1
    if op == "neg":
        return 31 << 26 | _reg(a[0], "r") << 21 | _reg(a[1], "r") << 16 | 104 << 1
    if op in ("extsb", "extsh"):
        return 31 << 26 | _reg(a[1], "r") << 21 | _reg(a[0], "r") << 16 | (954 if op == "extsb" else 922) << 1
    if op in _X_LOGIC:
        return 31 << 26 | _reg(a[1], "r") << 21 | _reg(a[0], "r") << 16 | _reg(a[2], "r") << 11 | _X_LOGIC[op] << 1
    if op in _X_MEMX:
        kind = "f" if op in ("lfsx", "stfsx") else "r"
        return 31 << 26 | _reg(a[0], kind) << 21 | _reg(a[1], "r") << 16 | _reg(a[2], "r") << 11 | _X_MEMX[op] << 1
    if op in ("cmpwi", "cmplwi", "cmpw", "cmplw"):
        crf = 0
        if len(a) == 3:
            crf = _reg(a[0], "cr")
            a = a[1:]
        if op == "cmpwi":
            return 11 << 26 | crf << 23 | _reg(a[0], "r") << 16 | _simm(ctx.value(a[1]), op)
        if op == "cmplwi":
            return 10 << 26 | crf << 23 | _reg(a[0], "r") << 16 | _uimm(ctx.value(a[1]), op)
        return 31 << 26 | crf << 23 | _reg(a[0], "r") << 16 | _reg(a[1], "r") << 11 | (0 if op == "cmpw" else 32) << 1
    if op in ("rlwinm", "rlwinm."):
        sh, mb, me = (ctx.value(x) for x in a[2:5])
        return 21 << 26 | _reg(a[1], "r") << 21 | _reg(a[0], "r") << 16 | sh << 11 | mb << 6 | me << 1 | (op == "rlwinm.")
    if op == "slwi":
        n = ctx.value(a[2])
        return _encode("rlwinm", [a[0], a[1], str(n), "0", str(31 - n)], pc, ctx)
    if op == "srwi":
        n = ctx.value(a[2])
        return _encode("rlwinm", [a[0], a[1], str((32 - n) & 31), str(n), "31"], pc, ctx)
    if op == "clrlwi":
        n = ctx.value(a[2])
        return _encode("rlwinm", [a[0], a[1], "0", str(n), "31"], pc, ctx)
    if op in ("srawi",):
        return 31 << 26 | _reg(a[1], "r") << 21 | _reg(a[0], "r") << 16 | ctx.value(a[2]) << 11 | 824 << 1
    if op in ("mflr", "mfctr"):
        return 31 << 26 | _reg(a[0], "r") << 21 | _spr_field(_SPR[op[2:]]) | 339 << 1
    if op in ("mtlr", "mtctr"):
        return 31 << 26 | _reg(a[0], "r") << 21 | _spr_field(_SPR[op[2:]]) | 467 << 1
    if op in ("b", "bl"):
        d = ctx.value(a[0]) - pc
        if ctx.final and not -0x2000000 <= d < 0x2000000:
            raise AsmError("branch out of range")
        return 18 << 26 | (d & 0x3FFFFFC) | (op == "bl")
    if op in _BC or op == "bdnz":
        if op == "bdnz":
            bo, bi, target = 16, 0, a[0]
        else:
            bo, bit = _BC[op]
            crf = 0
            if len(a) == 2:
                crf = _reg(a[0], "cr")
                a = a[1:]
            bi, target = 4 * crf + bit, a[0]
        d = ctx.value(target) - pc
        if ctx.final and not -0x8000 <= d < 0x8000:
            raise AsmError("conditional branch out of range")
        return 16 << 26 | bo << 21 | bi << 16 | (d & 0xFFFC)
    if op == "fcmpu":
        return 63 << 26 | _reg(a[0], "cr") << 23 | _reg(a[1], "f") << 16 | _reg(a[2], "f") << 11
    if op in _F_AB:
        o, x = _F_AB[op]
        return o << 26 | _reg(a[0], "f") << 21 | _reg(a[1], "f") << 16 | _reg(a[2], "f") << 11 | x << 1
    if op in _F_AC:
        o, x = _F_AC[op]
        return o << 26 | _reg(a[0], "f") << 21 | _reg(a[1], "f") << 16 | _reg(a[2], "f") << 6 | x << 1
    if op in _F_B:
        return 63 << 26 | _reg(a[0], "f") << 21 | _reg(a[1], "f") << 11 | _F_B[op] << 1
    raise AsmError("unknown instruction '%s'" % op)


def _parse(source):
    """Lines as (label or None, op or None, operands); comments after ';' or '#'."""
    for number, line in enumerate(source.splitlines(), 1):
        line = re.split(r"[;#]", line, 1)[0].strip()
        while line:
            m = re.match(r"^([A-Za-z_.$][\w.$]*):\s*(.*)$", line)
            if m:
                yield number, m.group(1), None, []
                line = m.group(2).strip()
                continue
            parts = line.split(None, 1)
            yield number, None, parts[0].lower(), _split_operands(parts[1]) if len(parts) > 1 else []
            break


def assemble(source, origin, symbols=None):
    """{address: word} for the source placed at origin. Labels join the symbols (the caller's dict is updated)."""
    symbols = {} if symbols is None else symbols
    lines = list(_parse(source))
    for final in (False, True):
        ctx = _Ctx(symbols, final)
        pc = origin
        words = {}
        for number, label, op, args in lines:
            try:
                if label:
                    symbols[label] = pc
                    continue
                if op == ".org":
                    pc = ctx.value(args[0])
                elif op == ".align":
                    n = ctx.value(args[0])
                    while pc % n:
                        words[pc] = 0
                        pc += 4
                elif op == ".space":
                    for _ in range(ctx.value(args[0]) // 4):
                        words[pc] = 0
                        pc += 4
                elif op == ".word":
                    for x in args:
                        words[pc] = ctx.value(x) & 0xFFFFFFFF
                        pc += 4
                elif op == ".float":
                    for x in args:
                        words[pc] = _float_bits(float(eval(x, {"__builtins__": {}}, dict(symbols))))
                        pc += 4
                else:
                    words[pc] = _encode(op, args, pc, ctx) & 0xFFFFFFFF
                    pc += 4
            except AsmError as e:
                raise AsmError("line %d: %s" % (number, e))
    return words
