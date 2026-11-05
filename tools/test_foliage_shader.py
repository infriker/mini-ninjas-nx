#!/usr/bin/env python3
"""Interpret the original and patched grass SPIR-V and compare discard/RGBA.

Usage: test_foliage_shader.py original.spv patched.spv
The C++ companion extracts these from the user's release shader library.
This deliberately executes the original control flow as the reference; it
does not restate the patch's alpha-test formula. Texture sampling is supplied
as an identical value to both modules, not an emulation of GPU filtering.
"""
from collections import Counter
import math
from pathlib import Path
import random
import struct
import sys


def f32(value):
    try:
        return struct.unpack('<f', struct.pack('<f', value))[0]
    except OverflowError:
        return math.copysign(math.inf, value)


def op_value(op, a, b):
    if isinstance(a, tuple):
        return tuple(op_value(op, x, y) for x, y in zip(a, b))
    if op == 129: return f32(a + b)
    if op == 133: return f32(a * b)
    if op == 170: return a == b
    if op == 171: return a != b
    if op == 194: return a >> b
    if op == 199: return a & b
    if math.isnan(a) or math.isnan(b): return False  # OpFOrd*, including NotEqual
    if op == 180: return a == b
    if op == 182: return a != b
    if op == 184: return a < b
    if op == 186: return a > b
    if op == 188: return a <= b
    if op == 190: return a >= b
    raise AssertionError(('unsupported arithmetic', op))


class Module:
    def __init__(self, path):
        raw = Path(path).read_bytes()
        words = struct.unpack('<%dI' % (len(raw) // 4), raw)
        assert words[0] == 0x07230203
        ins = []
        at = 5
        while at < len(words):
            n = words[at] >> 16
            assert n and at + n <= len(words)
            ins.append((words[at] & 65535, words[at + 1:at + n]))
            at += n
        start = next(i for i, (op, _) in enumerate(ins) if op == 54)
        self.globals, self.code = ins[:start], ins[start + 1:]
        self.labels = {a[0]: i for i, (op, a) in enumerate(self.code) if op == 248}
        self.decorations = {(a[0], a[1]): a[2] for op, a in self.globals if op == 71 and len(a) == 3}
        self.counts = Counter(op for op, _ in self.code)
        assert self.counts[87] == 1 and self.counts[252] == 1 and self.counts[62] == 1
        assert all(op not in (99, 207, 208, 209, 210, 211, 212, 213, 214, 215) for op, _ in self.code)

    def run(self, spec, texel, colour, uv, reference, fog):
        values, types, memory = {}, {}, {}
        output = None
        for op, a in self.globals:
            if op in (20, 21, 22, 23): types[a[0]] = (op, a[1:])
            elif op in (41, 42): values[a[1]] = op == 41
            elif op in (43, 50):
                t = types[a[0]][0]
                value = spec if op == 50 else a[2]
                values[a[1]] = struct.unpack('<f', struct.pack('<I', value))[0] if t == 22 else value
            elif op in (1, 46):
                t, shape = types[a[0]]
                values[a[1]] = tuple(0.0 for _ in range(shape[1])) if t == 23 else 0
            elif op == 52: values[a[1]] = op_value(a[2], values[a[3]], values[a[4]])
            elif op == 59:
                var, storage = a[1:3]
                values[var] = (var, ())
                binding = self.decorations.get((var, 33))
                location = self.decorations.get((var, 30))
                if storage == 1: memory[var] = uv if location == 0 else colour
                elif storage == 2:
                    constants = [(0.0,) * 4] * 224
                    if binding == 1: constants[0], constants[1] = fog, (0.625, 0.0, 0.0, 0.0)
                    else: constants[17] = (reference, 0.0, 0.0, 0.0)
                    memory[var] = (constants,)
                elif storage == 3: output = var
                elif storage == 0: memory[var] = var  # image/sampler identity
                else: raise AssertionError(('storage class', storage))
            elif op not in (3, 5, 6, 14, 15, 16, 17, 25, 26, 27, 28, 30, 32, 33, 71, 72):
                # Void types and debug records do not affect evaluation.
                assert op == 19, ('unsupported global instruction', op)
        pc, block, predecessor, samples = 0, None, None, 0
        while pc < len(self.code):
            op, a = self.code[pc]
            pc += 1
            if op == 248: block = a[0]
            elif op == 61:
                var, indices = values[a[2]]
                value = memory[var]
                for i in indices: value = value[i]
                values[a[1]] = value
            elif op == 65:
                var, indices = values[a[2]]
                values[a[1]] = (var, indices + tuple(values[i] for i in a[3:]))
            elif op == 79:
                both = values[a[2]] + values[a[3]]
                values[a[1]] = tuple(both[i] for i in a[4:])
            elif op == 81: values[a[1]] = values[a[2]][a[3]]
            elif op == 82:
                value = list(values[a[3]])
                value[a[4]] = values[a[2]]
                values[a[1]] = tuple(value)
            elif op == 86: values[a[1]] = (values[a[2]], values[a[3]])
            elif op == 87:
                values[a[1]] = texel
                samples += 1
            elif op == 127:
                value = values[a[2]]
                values[a[1]] = tuple(-v for v in value) if isinstance(value, tuple) else -value
            elif op in (129, 133, 170, 171, 180, 182, 184, 186, 188, 190, 194, 199):
                values[a[1]] = op_value(op, values[a[2]], values[a[3]])
            elif op == 169: values[a[1]] = values[a[3] if values[a[2]] else a[4]]
            elif op == 245:
                pairs = list(zip(a[2::2], a[3::2]))
                values[a[1]] = values[next(value for value, label in pairs if label == predecessor)]
            elif op in (249, 250, 251):
                if op == 249: target = a[0]
                elif op == 250: target = a[1] if values[a[0]] else a[2]
                else: target = dict(zip(a[2::2], a[3::2])).get(values[a[0]], a[1])
                predecessor, pc = block, self.labels[target]
            elif op == 247: pass
            elif op == 252: return None, samples
            elif op == 62:
                var, indices = values[a[0]]
                assert var == output and not indices
                memory[var] = values[a[1]]
            elif op == 253: return memory[output], samples
            else: raise AssertionError(('unsupported function instruction', op))
        raise AssertionError('no return')


def bits(values):
    if values is None: return None
    return tuple('nan' if math.isnan(v) else struct.pack('<f', v) for v in values)


def main():
    original, patched = (Module(p) for p in sys.argv[1:])
    assert original.counts[251] == 1 and patched.counts[251] == 0
    rng = random.Random(0x39F26D87)
    edges = [0.0, -0.0, 1.0, -1.0, math.inf, -math.inf, math.nan,
             f32(0.7960785), f32(0.7960785 - 1e-7), f32(0.7960785 + 1e-7)]
    cases = [(x, 1.0, ref) for ref in edges for x in edges]
    cases += [(f32(i / 255), fade, ref) for i in range(256)
              for fade in (0.0, 0.1, 0.5, 1.0) for ref in (0.0, f32(0.7960785), 1.0)]
    cases += [tuple(struct.unpack('<f', rng.getrandbits(32).to_bytes(4, 'little'))[0]
                    for _ in range(3)) for _ in range(256)]
    count = 0
    for enabled in (0, 2):
        for comparison in range(8):
            spec = 0x800300 | enabled | (comparison << 16)  # unrelated flags must have no effect
            for alpha, fade, reference in cases:
                texel = tuple(f32(rng.random()) for _ in range(3)) + (alpha,)
                colour = tuple(f32(rng.uniform(-1, 2)) for _ in range(3)) + (fade,)
                uv = (0.375, 0.625, f32(rng.uniform(-1, 2)), 0.0)
                fog = (0.25, 0.5, 0.75, 1.0)
                args = (spec, texel, colour, uv, reference, fog)
                before, bs = original.run(*args)
                after, ns = patched.run(*args)
                assert bs == ns == 1
                assert bits(before) == bits(after), (spec, alpha, fade, reference, before, after)
                count += 1
    print(f'Foliage SPIR-V semantics passed: {count} discard/RGBA cases, all eight comparisons,')
    print('alpha test enabled/disabled, fade, threshold edges, random floats, infinities and NaNs')


if __name__ == '__main__': main()
