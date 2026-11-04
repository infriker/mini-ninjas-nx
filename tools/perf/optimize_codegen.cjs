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
// Only a branch-and-link to the named entry, with an explicit guest LR save.
// A generated tail, fallthrough into a fragment, or indirect branch cannot match.
const linkedCall=/^(\s*\/\/ bl 0x([0-9a-f]{8})\r?\n[ \t]+ctx\.lr = 0x[0-9a-f]{8};\r?\n[ \t]+)(sub_[0-9a-f]{8})\(ctx, base\);/gim;
const sensitive=new Set(['sub_82402148','sub_82402628','sub_824027A8','sub_8241CA80','sub_8241CDA0','sub_82425830']);
const unsafeEntries=new Set();
// These two native hooks implement the supported XEX's exact routines.
const nativeOriginals=new Map([
  ['sub_82417FF0','8619c53824116a2c7f81448b0b79c6540c7ac3a725d9d0c067ebc0bb47d99c63'],
  ['sub_82501EF0','a5b6efbee5b401c54ea7176e682ffd86311698be01b974252d4312033f6df3de'],
]);
const original=new Map(), owners=new Map(), helpers=new Map();
for(const unit of units){const s=fs.readFileSync(path.join(input,unit),'utf8');original.set(unit,s);
  for(const match of s.matchAll(bodyPattern)){const body=match[0],name=body.match(bodyName)[1];
    if(owners.has(name))throw Error('duplicate generated definition '+name);
    owners.set(name,unit);
    if(nativeOriginals.has(name)){
      const digest=crypto.createHash('sha256').update(body.replace(/\r/g,'')).digest('hex');
      if(digest!==nativeOriginals.get(name))throw Error('native hook signature mismatch: '+name);
    }
    if(unsafeBody(name,body))unsafeEntries.add(name);
    if(helperName.test(name)&&!/(?:\bgoto\b|\bloc_|\b(?:MINININJAS_CAPTURE|setjmp|longjmp)\b|coroutines::)/.test(body))helpers.set(name,body);
  }
}
for(const name of nativeOriginals.keys())if(!owners.has(name))throw Error('missing native hook original: '+name);
// Exclude any symbol mentioned by native code, including address literals that
// might be passed to a dynamic hook. Read only source/header files, not generated.
const external=new Set([...sensitive,...unsafeEntries,...nativeOriginals.keys()]);
function scan(dir){if(!fs.existsSync(dir))return;for(const entry of fs.readdirSync(dir,{withFileTypes:true})){
  const p=path.join(dir,entry.name);if(entry.isDirectory()){if(entry.name!=='codegen')scan(p);continue;}
  if(!/\.(?:c|cc|cpp|cxx|h|hh|hpp|inc|inl|s)$/i.test(entry.name))continue;
  let s=fs.readFileSync(p,'utf8').replace(/\/\*[\s\S]*?\*\/|\/\/[^\n]*/g,' ');
  for(const m of s.matchAll(/\b(?:__imp__)?([A-Za-z_]\w*)\b/g))if(owners.has(m[1]))external.add(m[1]);
  for(const m of s.matchAll(/\b(?:__imp__)?(sub_[0-9A-Fa-f]{8})\b/g))external.add(m[1].toUpperCase().replace('SUB_','sub_'));
  // Also catch token-pasted hook names and suffixed address literals.
  for(const m of s.matchAll(/\b(?:0x)?(82[0-9a-f]{6})[ul]*\b/gi))external.add('sub_'+m[1].toUpperCase());
}}
for(const p of ['src','shaders','sdk/src','sdk/include']){
  if(!fs.existsSync(path.join(root,p)))throw Error('missing native sources for hook audit: '+p);
  scan(path.join(root,p));
}
const config=fs.readFileSync(path.join(root,'minininjas.toml'),'utf8').replace(/#[^\n]*/g,'');
for(const m of config.matchAll(/\b(?:sub_|0x)(82[0-9a-f]{6})\b/gi))external.add('sub_'+m[1].toUpperCase());
for(const [name,body] of helpers)if(external.has(name))helpers.delete(name);

function unsafeBody(name,body){
  return sensitive.has(name)||/\b(?:MINININJAS_CAPTURE|setjmp|longjmp|(?:__imp__)?(?:RtlUnwind|RtlCaptureContext|RtlRaiseException|KeBugCheck|KeBugCheckEx))\b|coroutines::|sub_8241CA80|sub_8241CDA0/.test(body);
}
// Stack access always stays volatile: a guest coroutine may copy or observe
// an older frame through transitive calls. No stack or register rewrite here.
const helperHeader=['// Derived from GPL-3.0-or-later Dante/Gears2 generated-helper optimization.','// See tools/perf/NOTICE. These copies preserve the original full PPCContext ABI.','#pragma once',''];
// Keep capture/restore frames and their entry points intact across the LTO link.
helperHeader.push('#if defined(__GNUC__) && !defined(__clang__)');
for(const name of [...unsafeEntries].sort()){
  helperHeader.push(`extern "C" void __imp__${name}(PPCContext& __restrict, uint8_t*) __attribute__((noipa));`);
}
helperHeader.push('#endif','');
for(const [name,raw] of [...helpers].sort((a,b)=>a[0].localeCompare(b[0]))){
  const inner=raw.replace(/^DEFINE_REX_FUNC\([^)]+\) \{\r?\n/,'').replace(/\r?\n\}\r?$/,'');
  helperHeader.push(`static inline __attribute__((always_inline)) void mini_perf_${name}(PPCContext& __restrict ctx, uint8_t* base) {`,inner,'}','');
}
const counts={functions:0,criticalSkipped:0,stack:0,inlineHelpers:0,directCalls:0,localCalls:0,crossUnitCalls:0,helperDefinitions:helpers.size};
fs.mkdirSync(output,{recursive:false});
for(const name of fs.readdirSync(input)){const src=path.join(input,name),dst=path.join(output,name);if(fs.statSync(src).isFile())fs.copyFileSync(src,dst);}
fs.writeFileSync(path.join(output,'minininjas_perf_helpers.h'),helperHeader.join('\n'));
for(const unit of units){
  const changed=original.get(unit).replace(bodyPattern, raw=>{
    counts.functions++;const name=raw.match(bodyName)[1];if(unsafeBody(name,raw)){counts.criticalSkipped++;return raw;}
    if(helperName.test(name))return raw; // no mutation of canonical save/restore helper definitions
    let body=raw;
    body=body.replace(callPattern,(m,callee)=>{
      if(helpers.has(callee)){counts.inlineHelpers++;return `mini_perf_${callee}(ctx, base);`;}
      return m;
    });
    return body.replace(linkedCall,(original,prefix,target,callee)=>{
      const owner=owners.get(callee);
      if(!owner || external.has(callee) || callee!=='sub_'+target.toUpperCase())return original;
      counts.directCalls++;
      if(owner===unit)counts.localCalls++;else counts.crossUnitCalls++;
      return prefix+'__imp__'+callee+'(ctx, base);';
    });
  });
  const newline=changed.indexOf('\n');if(newline<0)throw Error('bad source header '+unit);
  fs.writeFileSync(path.join(output,unit),changed.slice(0,newline+1)+'#include "minininjas_perf_helpers.h"\n'+changed.slice(newline+1));
}
const digest=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const report={input,output,counts,nativeOriginals:Object.fromEntries(nativeOriginals),excludedCritical:[...sensitive].sort(),externalExclusions:[...external].sort(),
  inputDigest:Object.fromEntries([...units,'minininjas_pch.h'].map(n=>[n,digest(path.join(input,n))])),
  outputDigest:Object.fromEntries([...units,'minininjas_pch.h','minininjas_perf_helpers.h'].map(n=>[n,digest(path.join(output,n))]))};
fs.writeFileSync(path.join(output,'perf-manifest.json'),JSON.stringify(report,null,2)+'\n');
console.log(JSON.stringify(counts));
