// SPDX-License-Identifier: GPL-3.0-or-later
// Mini Ninjas adaptation of the register-localization approach in Dante/Gears 2.
// Input is ReXGlue's generated code for the supported XEX. See NOTICE.
'use strict';
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const targets = require('./local_regs.json');

const registerNames = [
  ...['r', 'f', 'v', 'cr'].flatMap((prefix, i) =>
    Array.from({length: [32, 32, 128, 8][i]}, (_, n) => prefix + n)),
  'ctr', 'lr', 'xer',
];
const bits = new Map(registerNames.map((r, i) => [r, 1n << BigInt(i)]));
const all = (1n << BigInt(registerNames.length)) - 1n;
const opaque = {touch: all, write: all};
const reg = /\bctx\.(r\d+|f\d+|v\d+|cr\d+|ctr|lr|xer)\b/g;
const regsIn = (code) => [...code.matchAll(reg)].reduce((m, x) => m | bits.get(x[1]), 0n);
const local = (code) => code.replace(reg, (_, name) => 'local_' + name);
const helper = /^mini_perf_(__\w+)\(ctx, base\);$/;
const direct = /^(\w+)\(ctx, base\);$/;
const trap = /^(if \(.*\) )?(ppc_trap\(ctx, base, \d+\);)$/;
const indirect = /^REX_CALL_INDIRECT_FUNC\(.+\);$/;
const ret = /^(if \(.*\) )?return;$/;
const knownMacro = /^REX_(?:FUNC_PROLOGUE|CALL_INDIRECT_FUNC|RAW_ADDR|(?:LOAD|STORE)_U(?:8|16|32|64))$/;
const pureHelper = /^(?:simde_mm_\w+|__builtin_(?:rotateleft(?:32|64)|bswap(?:16|32|64)|clz\w*)|int(?:8|16|32|64)_t|uint(?:8|16|32|64)_t|float|double|int|char|if|for|switch|compare|setFromMask|set_raw|isnan|fma|sqrt|memset|memcpy|(?:enable|disable)FlushMode(?:Unconditional)?)$/;

function readFunctions(dir) {
  const functions = new Map();
  for (const file of fs.readdirSync(dir).filter(f => /^minininjas_recomp\.\d+\.cpp$/.test(f))) {
    const source = fs.readFileSync(path.join(dir, file), 'utf8').replace(/\r/g, '');
    for (const m of source.matchAll(/^DEFINE_REX_FUNC\((\w+)\) \{\n([\s\S]*?)^}/gm)) {
      if (functions.has(m[1])) throw Error('Duplicate guest function ' + m[1]);
      functions.set(m[1], {definition: m[0], body: m[2]});
    }
  }
  return functions;
}

function statements(body) {
  return body.split('\n').map(line => line.split('//', 1)[0].trim()).filter(Boolean);
}

// Only classify an obvious destination. Ambiguous instructions may dirty
// every register they mention, including partial SIMD and condition writes.
function writes(code, touch) {
  const dest = /^ctx\.(\w+)(?:\.\w+)?(?:\[[^\]]+\])?\s*=(?!=)/.exec(code);
  if (dest && !/(?:\+\+|--|simde_mm_store|memcpy|memset)/.test(code)) return bits.get(dest[1]) || 0n;
  const store = /^simde_mm_store_(?:si128|ps)\(\s*(?:\([^()]+\*\)\s*)?(?:&\s*)?ctx\.(v\d+)\b/.exec(code);
  if (store) return bits.get(store[1]);
  if (/^simde_mm_store_(?:si128|ps)\(\s*(?:\([^()]+\*\)\s*)?(?:REX_RAW_ADDR\(|&?vTemp\.)/.test(code)) return 0n;
  if (/^(?:temp\.\w+|ea|vTemp\.\w+\[\d+\]|simde__m128i \w+)\s*=/.test(code) &&
      !/(?:\+\+|--|simde_mm_store|memcpy|memset)/.test(code)) return 0n;
  const compare = /^ctx\.(cr\d+)\.(?:compare|setFromMask|set_raw)\b/.exec(code);
  if (compare) return bits.get(compare[1]);
  if (/^(?:if\b|switch\b|goto\b|return;|REX_STORE_U\d+\(|ea = )/.test(code) &&
      !/(?:\+\+|--|[+\-*/&|^]=|[^=!<>]=[^=])/.test(code.replace(/^ea = /, ''))) return 0n;
  return touch;
}

// A full unconditional definition kills the previous value. Partial writes
// to GPRs, XER, CR fields and vector lanes retain the other bits.
function inputs(code, used) {
  const assignment = /^ctx\.(?:(r\d+|f\d+|ctr)\.(?:[su]64|f64)|(lr|v\d+))\s*=\s*(?!=)/.exec(code);
  const compare = /^ctx\.(cr\d+)\.(?:compare|setFromMask|set_raw)\b/.exec(code);
  const vector = /^simde_mm_store_(?:si128|ps)\(\s*(?:\([^()]+\*\)\s*)?ctx\.(v\d+)\.\w+\s*,/.exec(code);
  const m = assignment || compare || vector;
  return m ? {read: regsIn(code.slice(m[0].length)), kill: bits.get(m[1] || m[2])} : {read: used, kill: 0n};
}

function analyze(name, functions) {
  const source = functions.get(name);
  if (!source) return null;
  const lines = statements(source.body).flatMap(line => {
    const inlined = helper.exec(line);
    if (!inlined) return [line];
    const original = functions.get(inlined[1]);
    if (!original) throw Error('Missing inline helper ' + inlined[1]);
    const inner = statements(original.body).filter(s => s !== 'REX_FUNC_PROLOGUE();');
    if (inner.at(-1) === 'return;') inner.pop();
    if (inner.some(s => /\b(?:goto|return|if|switch)\b|\(ctx, base\)/.test(s))) throw Error('Nonlinear save helper ' + inlined[1]);
    return ['{', ...inner, '}'];
  });
  const nodes = [];
  let touch = 0n, write = 0n;
  for (const code of lines) {
    const call = direct.exec(code)?.[1];
    const trapping = trap.exec(code);
    const dispatch = indirect.test(code);
    const boundary = !!(call || trapping || dispatch || code === '__builtin_trap();');
    // FPSCR/MSR always live in the real context. Reject raw context users,
    // atomics, coroutine machinery and unfamiliar macros instead of guessing.
    const rest = code.replace(reg, '').replace(/\bctx\.(?:fpscr|msr)\b/g, '')
      .replace(/\b\w+\(ctx, base\);|ppc_trap\(ctx, base, \d+\);/g, '');
    if (/\bctx\b|setjmp|longjmp|coroutines::|SEH_|MINININJAS_|__atomic_|__sync_/.test(rest) ||
        [...code.matchAll(/\b(REX_\w+)\(/g)].some(m => !knownMacro.test(m[1]))) return null;
    if (!boundary && /\(ctx, base/.test(code)) return null;
    if (!boundary && [...code.matchAll(/\b([A-Za-z_]\w*)\s*\(/g)]
      .some(m => !knownMacro.test(m[1]) && !pureHelper.test(m[1]))) return null;
    const used = regsIn(code);
    const changed = boundary ? 0n : writes(code, used);
    touch |= used;
    write |= changed;
    nodes.push({code, call, trapping, dispatch, boundary, changed, ...inputs(code, used)});
  }
  return {name, nodes, touch, write};
}

function generate(dir, output, dispatchOutput) {
  const functions = readFunctions(dir);
  const info = new Map();
  const get = name => {
    if (!info.has(name)) info.set(name, analyze(name, functions));
    return info.get(name);
  };
  const roots = Object.keys(targets);
  for (const name of roots) {
    const definition = functions.get(name)?.definition;
    if (!definition) throw Error('Missing register-localization target ' + name);
    const canonical = definition.replace(/\bmini_perf_(__\w+)|\b__imp__(sub_[A-F0-9]{8})/g, (_, a, b) => a || b);
    if (crypto.createHash('sha256').update(canonical).digest('hex') !== targets[name] || !get(name)) {
      throw Error('Unsupported function body for register localization: ' + name);
    }
  }

  // A core operation may touch the caller's entire state through an import,
  // native hook, indirect jump or coroutine. Only narrow a direct callee's
  // effects inside the selected region. Other entry points keep a fully
  // materialized context even when their current implementation is simple.
  const summaries = new Map();
  function effects(node) {
    if (!node.call?.startsWith('__imp__sub_')) return opaque;
    const name = node.call.slice('__imp__'.length);
    if (!leaves.has(name) && !targets[name]) return opaque;
    if (summaries.has(name)) return summaries.get(name);
    summaries.set(name, opaque); // conservative if this call graph is recursive
    const body = get(name);
    if (!body) return opaque;
    let {touch, write} = body;
    for (const child of body.nodes) {
      if (!child.boundary) continue;
      const nested = effects(child);
      touch |= nested.touch; write |= nested.write;
      if (write === all) break;
    }
    const result = {touch, write};
    summaries.set(name, result);
    return result;
  }

  const leaves = new Set();
  for (const name of roots) {
    for (const {call} of get(name).nodes) {
      if (!call?.startsWith('__imp__sub_')) continue;
      const leaf = call.slice('__imp__'.length);
      const f = get(leaf);
      if (f && f.touch && !f.nodes.some(n => n.boundary) && !targets[leaf]) leaves.add(leaf);
    }
  }
  const selected = [...leaves, ...roots];
  const declarations = new Set(roots.map(name => '__imp__' + name));
  for (const name of selected) {
    for (const {call} of get(name).nodes) if (call) declarations.add(call);
  }

  const out = ['// Generated from the local game translation. Do not edit.', '#include "minininjas_pch.h"', ''];
  // Keep the startup switch in a separate non-LTO object. Both paths remain
  // addressable, including indirect entry points and profile-trained originals.
  const dispatch = ['#include "minininjas_pch.h"', '#include "minininjas_native.h"', ''];
  for (const name of [...declarations].sort()) out.push('REX_EXTERN(' + name + ');');
  const signature = name => (targets[name] ? 'REX_EXTERN(' : 'static REX_FUNC(') + 'local_regs_' + name + ')';
  for (const name of selected) out.push(signature(name) + ';');

  for (const name of selected) {
    const f = get(name);
    const usedNames = registerNames.filter(r => f.touch & bits.get(r));
    const {incoming, outgoing, cuts, labels, succ} = registerFlow(f, effects);
    const transfers = (mask, store) => usedNames.filter(r => mask & bits.get(r)).map(r =>
      store ? 'ctx.' + r + ' = local_' + r + ';' : 'local_' + r + ' = ctx.' + r + ';');
    out.push('', signature(name) + ' {', '  REX_FUNC_PROLOGUE();');
    for (const r of usedNames) out.push('  auto local_' + r + ' = ctx.' + r + ';');
    for (const [i, n] of f.nodes.entries()) {
      if (n.code === 'REX_FUNC_PROLOGUE();') continue;
      const dirty = incoming[i];
      const returning = ret.exec(n.code);
      if (n.boundary || returning) {
        const condition = returning?.[1] || n.trapping?.[1] || '';
        out.push('  ' + local(condition) + '{');
        if (returning) out.push(...transfers(dirty, true).map(s => '    ' + s), '    return;');
        else {
          const impact = effects(n);
          out.push(...transfers(dirty & impact.touch, true).map(s => '    ' + s));
          let invocation = n.trapping?.[2] || local(n.code);
          const callee = n.call?.replace(/^__imp__/, '');
          // A selected root is always the same startup-selected mode; strong
          // unrelated hooks and indirect calls keep their original dispatch.
          if ((callee && targets[callee]) || (n.call?.startsWith('__imp__') && leaves.has(callee))) {
            invocation = 'local_regs_' + callee + '(ctx, base);';
          }
          out.push('    ' + invocation, ...transfers(impact.write, false).map(s => '    ' + s));
        }
        out.push('  }');
      } else {
        const jump = /^(if \(.*\) )?goto (loc_[A-F0-9]+);$/.exec(n.code);
        const edge = jump ? outgoing[i] & cuts[labels.get(jump[2])] : 0n;
        if (edge) {
          out.push('  ' + local(jump[1] || '') + '{', ...transfers(edge, true).map(s => '    ' + s),
            '    goto ' + jump[2] + ';', '  }');
        } else out.push('  ' + local(n.code));
        if (cuts[i]) out.push(...transfers(cuts[i], false).map(s => '  ' + s));
      }
      if (succ[i].includes(i + 1)) out.push(...transfers(outgoing[i] & cuts[i + 1], true).map(s => '  ' + s));
    }
    out.push(...transfers(incoming[f.nodes.length], true).map(s => '  ' + s), '}');
    if (targets[name]) {
      dispatch.push('REX_EXTERN(__imp__' + name + ');', signature(name) + ';',
        'REX_EXTERN(' + name + ') {', '  if (minininjas::cpu::local_registers_enabled) {',
        '    local_regs_' + name + '(ctx, base);', '  } else {', '    __imp__' + name + '(ctx, base);', '  }', '}');
    }
  }
  fs.writeFileSync(output, out.join('\n') + '\n');
  fs.writeFileSync(dispatchOutput, dispatch.join('\n') + '\n');
  console.log('Localized ' + roots.length + ' profiled entries and ' + leaves.size + ' direct leaf routines.');
}

// Forward may-dirty dataflow. Keep both paths at conditional jumps and traps.
// The generator emits computed jump tables as a C++ switch and short VMX
// byte-copy loops as a for followed by one statement.
function registerFlow(f, effects) {
  const nodes = f.nodes, n = nodes.length;
  const succ = nodes.map((_, i) => [i + 1]);
  const labels = new Map();
  nodes.forEach(({code}, i) => { if (/^loc_[A-F0-9]+:$/.test(code)) labels.set(code.slice(0, -1), i); });
  let jumpTable = null;
  nodes.forEach(({code}, i) => {
    const jump = /^(if \(.*\) )?goto (loc_[A-F0-9]+);$/.exec(code);
    if (jump) {
      if (!labels.has(jump[2])) throw Error(f.name + ': missing label ' + jump[2]);
      succ[i] = jump[1] ? [i + 1, labels.get(jump[2])] : [labels.get(jump[2])];
    } else if (ret.test(code)) succ[i] = code === 'return;' ? [n] : [i + 1, n];
    else if (code === '__builtin_trap();') succ[i] = [];
    else if (/^switch \(.*\) \{$/.test(code)) { jumpTable = i; succ[i] = []; }
    else if (/^(?:case .+|default):$/.test(code)) {
      if (jumpTable === null) throw Error(f.name + ': unexpected case label');
      succ[jumpTable].push(i);
    } else if (code === '}' && jumpTable !== null) {
      succ[jumpTable].push(i + 1); jumpTable = null;
    } else if (/^for \(/.test(code)) {
      const body = nodes[i + 1];
      if (!body || body.boundary || !body.code.endsWith(';') || /goto|return;/.test(body.code)) {
        throw Error(f.name + ': unsupported loop body');
      }
      succ[i].push(i + 2);
      succ[i + 1].push(i);
    } else if (/^(?:else|while|do\b|break;|continue;|if\b.*\{)|\bgoto\b/.test(code)) {
      throw Error(f.name + ': unsupported control flow ' + code);
    }
  });
  if (jumpTable !== null) throw Error(f.name + ': unterminated switch');
  const predecessors = Array.from({length: n + 1}, () => []);
  succ.forEach((edges,i) => edges.forEach(j => predecessors[j].push(i)));
  const impact = nodes.map(node => node.boundary ? effects(node).touch : 0n);
  const live = Array(n + 1).fill(0n);
  live[n] = f.touch; // all guest-visible results survive a public return
  const reverse = nodes.map((_,i) => i);
  while (reverse.length) {
    const i = reverse.pop();
    const after = succ[i].reduce((m,j) => m | live[j], 0n);
    const before = (after & ~nodes[i].kill) | nodes[i].read | (impact[i] & f.touch);
    if (live[i] !== before) { live[i] = before; reverse.push(...predecessors[i]); }
  }
  const cuts = Array(n + 1).fill(0n);
  const cleared = nodes.map(node => node.boundary && !node.trapping?.[1] ? effects(node).touch : 0n);
  function forward() {
    const incoming = Array(n + 1).fill(0n), outgoing = Array(n).fill(0n), seen = new Set(), todo = [0];
    while (todo.length) {
      const i = todo.pop();
      if (i === n) continue;
      outgoing[i] = (incoming[i] | nodes[i].changed) & ~cleared[i];
      for (const j of succ[i]) {
        const merged = incoming[j] | (outgoing[i] & live[j] & ~cuts[j]);
        if (merged !== incoming[j] || !seen.has(j)) { incoming[j] = merged; seen.add(j); todo.push(j); }
      }
    }
    return {incoming, outgoing};
  }
  // At forward joins, pay for path-specific live results on the edge that
  // produced them. Cold early exits shouldn't save an entire hot path's VMX set.
  // Keep backward edges untouched so a live loop value stays in a host register.
  const first = forward();
  for (const j of labels.values()) {
    const pred = predecessors[j];
    if (pred.length < 2 || pred.some(i => i >= j)) continue;
    const common = pred.reduce((m,i) => m & first.outgoing[i], all);
    cuts[j] = first.incoming[j] & ~common;
  }
  const result = forward();
  // Return nodes already emit their own writeback, including conditional
  // returns. Only a real fallthrough uses the trailing end-of-function flush.
  result.incoming[n] = predecessors[n].filter(i => !ret.test(nodes[i].code))
    .reduce((m,i) => m | result.outgoing[i], 0n);
  return {...result, cuts, labels, succ};
}

if (require.main === module) {
  const [dir, output, dispatch] = process.argv.slice(2);
  if (!dir || !output || !dispatch) throw Error('Usage: node localize_regs.cjs generated-dir bodies.cpp dispatch.cpp');
  generate(dir, output, dispatch);
}
