/*
 * Mini Ninjas generated-code optimization. Inspired by
 * VasilkovND/dantes-inferno-nx, dante/tools/patch_{stack_access,inline_calls}.py,
 * GPL-3.0-or-later; see NOTICE and LICENSE in this directory.
 *
 * Usage: node tools/perf/optimize_codegen.cjs [input-dir] [new-output-dir]
 * Refuses to overwrite an existing output. The original tree is never modified.
 */
const fs=require('fs'), path=require('path'), crypto=require('crypto');
const root=path.resolve(__dirname,'../..');
const input=path.resolve(root,process.argv[2]||'generated');
const output=path.resolve(root,process.argv[3]||'generated-perf');
if(input===output || fs.existsSync(output))throw Error('output must be a new, distinct directory');
if(!output.startsWith(root+path.sep))throw Error('output must be inside the workspace');
const units=fs.readdirSync(input).filter(x=>/^minininjas_recomp\.\d+\.cpp$/.test(x)).sort((a,b)=>a.localeCompare(b,'en',{numeric:true}));
if(!units.length || !fs.existsSync(path.join(input,'sources.cmake')))throw Error('invalid generated tree');
const bodyPattern=/^DEFINE_REX_FUNC\(([^)]+)\) \{\r?\n[\s\S]*?^\}\r?$/gm;
const bodyName=/^DEFINE_REX_FUNC\(([^)]+)\)/;
const helperName=/^__(?:save|rest)(?:gprlr|gpr|fpr|vmx)_\d+$/;
const callPattern=/(?<![\w])([A-Za-z_]\w*)\(ctx, base\);/g;
const sensitive=new Set(['sub_82402148','sub_82402628','sub_824027A8','sub_8241CA80','sub_8241CDA0','sub_82425830']);
const original=new Map(), owners=new Map(), helpers=new Map();
for(const unit of units){const s=fs.readFileSync(path.join(input,unit),'utf8');original.set(unit,s);
  for(const match of s.matchAll(bodyPattern)){const body=match[0],name=body.match(bodyName)[1];
    if(owners.has(name))throw Error('duplicate generated definition '+name);
    owners.set(name,unit);
    if(helperName.test(name)&&!/(?:\bgoto\b|\bloc_|\b(?:MINININJAS_CAPTURE|setjmp|longjmp)\b|coroutines::)/.test(body))helpers.set(name,body);
  }
}
// Exclude any symbol mentioned by native code, including address literals that
// might be passed to a dynamic hook. Read only source/header files, not generated.
const external=new Set(sensitive);
function scan(dir){if(!fs.existsSync(dir))return;for(const entry of fs.readdirSync(dir,{withFileTypes:true})){
  const p=path.join(dir,entry.name);if(entry.isDirectory()){if(entry.name!=='codegen')scan(p);continue;}
  if(!/\.(?:c|cc|cpp|h|hpp|inc)$/.test(entry.name))continue;
  let s=fs.readFileSync(p,'utf8').replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g,' ');
  for(const m of s.matchAll(/\b(?:__imp__)?([A-Za-z_]\w*)\b/g))if(owners.has(m[1]))external.add(m[1]);
  for(const m of s.matchAll(/\b(?:__imp__)?(sub_[0-9A-Fa-f]{8})\b/g))external.add(m[1].toUpperCase().replace('SUB_','sub_'));
  for(const m of s.matchAll(/\b0x(82[0-9a-fA-F]{6})\b/g))external.add('sub_'+m[1].toUpperCase());
}}
for(const p of ['src','sdk/src','sdk/include'])scan(path.join(root,p));
for(const [name,body] of helpers)if(external.has(name))helpers.delete(name);

function unsafeBody(name,body){
  return sensitive.has(name)||/\b(?:MINININJAS_CAPTURE|setjmp|longjmp|(?:__imp__)?(?:RtlUnwind|RtlCaptureContext|RtlRaiseException|KeBugCheck|KeBugCheckEx))\b|coroutines::|sub_8241CA80|sub_8241CDA0/.test(body);
}
// Stack access always stays volatile: a guest coroutine may copy or observe
// an older frame through transitive calls. No stack or register rewrite here.
const helperHeader=['// Derived from GPL-3.0-or-later Dante/Gears2 generated-helper optimization.','// See tools/perf/NOTICE. These copies preserve the original full PPCContext ABI.','#pragma once',''];
for(const [name,raw] of [...helpers].sort((a,b)=>a[0].localeCompare(b[0]))){
  const inner=raw.replace(/^DEFINE_REX_FUNC\([^)]+\) \{\r?\n/,'').replace(/\r?\n\}\r?$/,'');
  helperHeader.push(`static inline __attribute__((always_inline)) void mini_perf_${name}(PPCContext& __restrict ctx, uint8_t* base) {`,inner,'}','');
}
const counts={functions:0,criticalSkipped:0,stack:0,inlineHelpers:0,directCalls:0,helperDefinitions:helpers.size};
fs.mkdirSync(output,{recursive:false});
for(const name of fs.readdirSync(input)){const src=path.join(input,name),dst=path.join(output,name);if(fs.statSync(src).isFile())fs.copyFileSync(src,dst);}
fs.writeFileSync(path.join(output,'minininjas_perf_helpers.h'),helperHeader.join('\n'));
for(const unit of units){const local=new Set([...owners].filter(x=>x[1]===unit).map(x=>x[0]));
  const changed=original.get(unit).replace(bodyPattern, raw=>{
    counts.functions++;const name=raw.match(bodyName)[1];if(unsafeBody(name,raw)){counts.criticalSkipped++;return raw;}
    if(helperName.test(name))return raw; // no mutation of canonical save/restore helper definitions
    let body=raw;
    body=body.replace(callPattern,(m,callee)=>{
      if(helpers.has(callee)){counts.inlineHelpers++;return `mini_perf_${callee}(ctx, base);`;}
      if(local.has(callee)&&!external.has(callee)&&!helperName.test(callee)){counts.directCalls++;return `__imp__${callee}(ctx, base);`;}
      return m;
    });
    return body;
  });
  const newline=changed.indexOf('\n');if(newline<0)throw Error('bad source header '+unit);
  fs.writeFileSync(path.join(output,unit),changed.slice(0,newline+1)+'#include "minininjas_perf_helpers.h"\n'+changed.slice(newline+1));
}
const digest=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const report={input,output,counts,excludedCritical:[...sensitive].sort(),externalExclusions:[...external].sort(),
  inputDigest:Object.fromEntries([...units,'minininjas_pch.h'].map(n=>[n,digest(path.join(input,n))])),
  outputDigest:Object.fromEntries([...units,'minininjas_pch.h','minininjas_perf_helpers.h'].map(n=>[n,digest(path.join(output,n))]))};
fs.writeFileSync(path.join(output,'perf-manifest.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(counts));
