#!/usr/bin/env python3
"""Patch six proven Mini Ninjas coroutine sites; refuse changed or partial input."""
import argparse
import hashlib
import re
from pathlib import Path

HEADER = '#include "minininjas_coroutines.h"  // MINI_COROUTINE_BRIDGE\n'
SCOPE = '\t::minininjas::coroutines::CaptureScope mini_jump_scope; // MINI_COROUTINE_SCOPE\n'
GUARD = '\t::minininjas::coroutines::CheckGuestRestoreAllowed(); // MINI_COROUTINE_GUARD\n'

def call(lr, old, new):
    prefix=f'\tctx.lr = 0x{lr};\n\t'
    return (prefix+old+'(ctx, base);', prefix+new)

CAPTURE='MINININJAS_CAPTURE(mini_jump_scope, ctx, base, sub_8241CDA0);'
JUMP='::minininjas::coroutines::Jump(ctx, base, sub_8241CA80);'
LAUNCH='::minininjas::coroutines::Launch(ctx, base, sub_824027A8);'
PLAN = {
    '82402148': [call('824022F8','sub_8241CDA0',CAPTURE),
                 call('824023D8','sub_8241CA80',JUMP),
                 call('82402430','sub_824027A8',LAUNCH)],
    '82402628': [call('8240264C','sub_8241CDA0',CAPTURE),
                 call('824026E4','sub_8241CA80',JUMP)],
    '824027A8': [call('82402868','sub_8241CA80',JUMP)],
    '8241CA80': [],
}

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('directory',type=Path,nargs='?',default=Path('generated'))
    directory=parser.parse_args().directory
    files=list(directory.glob('minininjas_recomp.*.cpp'))
    if not files:raise ValueError(f'no generated translation units under {directory}')
    sources={p:p.read_text() for p in files}
    owners={}
    for name in PLAN:
        needle=f'DEFINE_REX_FUNC(sub_{name}) {{'
        found=[p for p,s in sources.items() if needle in s]
        if len(found)!=1:raise ValueError(f'{needle}: found {len(found)} functions')
        owners[name]=found[0]
    changed={}
    status=[]
    for name,edits in PLAN.items():
        path=owners[name]; text=changed.get(path,sources[path])
        start=text.index(f'DEFINE_REX_FUNC(sub_{name}) {{')
        end=text.find('\nDEFINE_REX_FUNC(',start+1)
        if end<0:end=len(text)
        block=text[start:end]
        for before,after in edits:
            old,new=block.count(before),block.count(after)
            if (old,new) not in ((1,0),(0,1)):
                raise ValueError(f'{name}: unexpected count old={old} new={new} for {before}')
            status.append('old' if old else 'new')
        marker=SCOPE if name in ('82402148','82402628') else GUARD if name=='8241CA80' else None
        if marker:
            if block.count(marker) not in (0,1):raise ValueError(f'duplicate marker {name}')
            status.append('new' if marker in block else 'old')
        changed[path]=text
    if len(set(status))!=1:raise ValueError('partially patched generated code; regenerate cleanly')
    if status[0]=='new':
        for path in set(owners.values()):
            if sources[path].count(HEADER)!=1:raise ValueError(f'missing/duplicate bridge include: {path}')
        print('Coroutine patch already applied and verified')
        return
    for name,edits in PLAN.items():
        path=owners[name]; text=changed[path]
        start=text.index(f'DEFINE_REX_FUNC(sub_{name}) {{')
        end=text.find('\nDEFINE_REX_FUNC(',start+1)
        if end<0:end=len(text)
        block=text[start:end]
        if name in ('82402148','82402628'):
            block=block.replace('\tREX_FUNC_PROLOGUE();\n','\tREX_FUNC_PROLOGUE();\n'+SCOPE,1)
        if name=='8241CA80':
            block=block.replace('\tREX_FUNC_PROLOGUE();\n','\tREX_FUNC_PROLOGUE();\n'+GUARD,1)
        for before,after in edits:block=block.replace(before,after,1)
        changed[path]=text[:start]+block+text[end:]
    for path,text in changed.items():
        if HEADER in text:raise ValueError(f'unexpected existing include {path}')
        first=text.find('\n')
        if first<0:raise ValueError(f'bad source header {path}')
        changed[path]=text[:first+1]+HEADER+text[first+1:]
    # All checks complete before any writes; exact original file digests are logged.
    for path,text in changed.items():
        digest=hashlib.sha256(sources[path].encode()).hexdigest()
        path.write_text(text)
        print(f'{path}: original sha256 {digest}')
    print(f'Patched {len(changed)} units: 2 saves, 3 jumps, 1 launch, 1 unsupported-jump guard')

if __name__=='__main__':main()
