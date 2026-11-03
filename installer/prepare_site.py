#!/usr/bin/env python3
"""Build a Pages directory from pinned release assets, without game files."""
import argparse
import hashlib
import json
import shutil
import zipfile
from pathlib import Path, PurePosixPath

HERE = Path(__file__).resolve().parent
SAFE_FILES = {'minininjas_shaders.dslib', 'shadercache_dksh.bin', 'LICENSES.txt',
              'LICENSE', 'NOTICE', 'LICENSE.txt', 'THIRD_PARTY_NOTICES.md', 'minininjas.toml'}
MAX_FILE = 128 * 1024 * 1024

def digest(data):
    return hashlib.sha256(data).hexdigest()

def checked_file(path, expected):
    if not isinstance(expected, str) or len(expected) != 64 or any(c not in '0123456789abcdef' for c in expected):
        raise ValueError('release SHA-256 is missing or invalid')
    data = Path(path).read_bytes()
    if digest(data) != expected:
        raise ValueError(f'SHA-256 mismatch: {path}')
    return data

def prepare(nro_path, runtime_path, output):
    manifest = json.loads((HERE / 'release/manifest.json').read_text(encoding='utf-8'))
    if len(manifest.get('builds', [])) != 1 or not manifest.get('tag'):
        raise ValueError('one pinned release tag and build are required')
    build = manifest['builds'][0]
    if build.get('nro') != 'mini-ninjas.nro' or build.get('runtime') != 'mini-ninjas-runtime.zip':
        raise ValueError('unexpected release asset name')
    nro = checked_file(nro_path, build.get('nro_sha256'))
    checked_file(runtime_path, build.get('runtime_sha256'))
    out = Path(output)
    if out.exists():
        raise ValueError('output directory already exists')
    shutil.copytree(HERE, out, ignore=shutil.ignore_patterns('prepare_site.py', '__pycache__'))
    (out / 'release' / build['nro']).write_bytes(nro)
    runtime_dir = out / 'release' / 'runtime'
    runtime_dir.mkdir()
    records = []
    seen = set()
    with zipfile.ZipFile(runtime_path) as archive:
        for info in archive.infolist():
            name = info.filename
            if info.is_dir():
                continue
            if name.startswith('mini-ninjas/'):
                name = name[len('mini-ninjas/'):]
            path = PurePosixPath(name)
            if ('\\' in name or path.is_absolute() or any(part in ('', '.', '..') for part in name.split('/'))
                    or not (name in SAFE_FILES or (name.startswith('LICENSES/') and len(path.parts) > 1))):
                raise ValueError(f'unsafe or unexpected runtime file: {name}')
            if name.lower() in seen or info.file_size > MAX_FILE:
                raise ValueError(f'duplicate or oversized runtime file: {name}')
            seen.add(name.lower())
            data = archive.read(info)
            target = runtime_dir.joinpath(*path.parts)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            records.append({'path': name, 'size': len(data), 'sha256': digest(data)})
    if 'minininjas_shaders.dslib' not in seen:
        raise ValueError('runtime archive is missing minininjas_shaders.dslib')
    build['runtime_files'] = sorted(records, key=lambda item: item['path'])
    (out / 'release' / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--nro', required=True)
    parser.add_argument('--runtime', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prepare(args.nro, args.runtime, args.output)
