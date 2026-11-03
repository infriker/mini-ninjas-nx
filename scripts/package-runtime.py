#!/usr/bin/env python3
"""Package the supplied shader programs and notices; never copy game files."""
import argparse
import hashlib
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]
LIBRARY_SHA256 = '9f9f3a51fce53fa3989099b1a3cb9cbdfc6141b80c215511979479a4fb5938cb'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--shaders', required=True, type=Path,
                        help='Directory containing the matching DSLIB and compiled DKSH cache')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists():
        raise SystemExit('Output already exists; choose a new path.')
    library = args.shaders / 'minininjas_shaders.dslib'
    cache = args.shaders / 'shadercache_dksh.bin'
    if hashlib.sha256(library.read_bytes()).hexdigest() != LIBRARY_SHA256:
        raise SystemExit('Unsupported shader library.')
    if not cache.is_file() or cache.stat().st_size == 0:
        raise SystemExit('A precompiled shader cache is required for a release package.')
    files = {
        'minininjas_shaders.dslib': library,
        'shadercache_dksh.bin': cache,
        'LICENSE': ROOT / 'LICENSE',
        'NOTICE': ROOT / 'NOTICE',
        'minininjas.toml': ROOT / 'resources/runtime/minininjas.toml',
    }
    for path in sorted((ROOT / 'LICENSES').rglob('*')):
        if path.is_file():
            if path.is_symlink():
                raise SystemExit('License symlinks are not accepted.')
            files[path.relative_to(ROOT).as_posix()] = path
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, path in sorted(files.items()):
            item = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            item.compress_type = zipfile.ZIP_DEFLATED
            item.external_attr = 0o100644 << 16
            archive.writestr(item, path.read_bytes(), compresslevel=9)
    print(f'{hashlib.sha256(args.output.read_bytes()).hexdigest()}  {args.output.name}')


if __name__ == '__main__':
    main()
