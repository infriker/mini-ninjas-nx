// Adapted from F.E.A.R. NX and nfsmw-nx-installer, GPL-3.0; see NOTICES.md.
import { ZipWriter, zipSize } from './zip.js';
import { t } from './i18n.js';
import { GAME_FILES } from './game-files.js';

const CHUNK = 16 * 1024 * 1024;
export const ROOT = 'mini-ninjas';
export const TITLE_ID = 0x534307e4;

export async function sha256(bytes) {
  const digest = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  return [...digest].map((b) => b.toString(16).padStart(2, '0')).join('');
}

async function* chunksOf(file) {
  for (let offset = 0; offset < file.size; offset += CHUNK) {
    yield await file.read(offset, Math.min(CHUNK, file.size - offset));
  }
}

export function readTitleId(xex) {
  if (xex.length < 24 || String.fromCharCode(...xex.subarray(0, 4)) !== 'XEX2') return null;
  const view = new DataView(xex.buffer, xex.byteOffset, xex.byteLength);
  const count = view.getUint32(0x14);
  for (let i = 0; i < count && 0x18 + i * 8 + 8 <= xex.length; i++) {
    const key = view.getUint32(0x18 + i * 8);
    const value = view.getUint32(0x18 + i * 8 + 4);
    if (key === 0x00040006 && value + 16 <= xex.length) return view.getUint32(value + 12);
  }
  return null;
}

export function programPath(build) {
  return `./release/${build.nro}?sha256=${build.nro_sha256}`;
}
export function runtimePath(file) {
  return `./release/runtime/${file.path.split('/').map(encodeURIComponent).join('/')}?sha256=${file.sha256}`;
}

export function rootAtExecutable(files) {
  const xex = files.filter((f) => /(^|\/)default\.xex$/i.test(f.path))
    .sort((a, b) => a.path.split('/').length - b.path.split('/').length)[0];
  if (!xex) return files;
  const prefix = xex.path.slice(0, xex.path.length - 'default.xex'.length);
  return files.filter((f) => f.path.startsWith(prefix))
    .map((f) => ({ ...f, path: f.path.slice(prefix.length) }));
}

// Require every file of this revision with its known size, not just a marker.
// The game data itself never leaves the browser; metadata is not game data.
export async function identifyGame(files, manifest) {
  const byName = new Map();
  for (const f of files) {
    const key = f.path.toLowerCase();
    if (byName.has(key)) throw new Error(t('duplicateFile', { path: f.path }));
    byName.set(key, f);
  }
  const xexFile = byName.get('default.xex');
  if (!xexFile) { const e = new Error(t('noExecutable')); e.code = 'noExecutable'; throw e; }
  const xex = await xexFile.read(0, xexFile.size);
  const xexHash = await sha256(xex);
  const build = manifest.builds.find((b) => b.xex_sha256 === xexHash) || null;
  const missing = GAME_FILES.filter((f) => {
    const found = byName.get(f.path.toLowerCase());
    return !found || found.size !== f.size;
  });
  const game = GAME_FILES.map((f) => byName.get(f.path.toLowerCase())).filter(Boolean);
  return { xexHash, xexSize: xex.length, titleId: readTitleId(xex), build, complete: !missing.length, missing, game };
}

export async function createPackage(files, release, sink, log = () => {}, progress = () => {}, onSize = () => {},
  { update = false } = {}) {
  const { xexHash, build, complete, game } = await identifyGame(files, release.manifest);
  if (!build) throw new Error(t('notSupported', { hash: xexHash }));
  if (!complete && !update) throw new Error(t('incomplete'));
  log(t('logRelease', { edition: build.edition }));
  if (update) log(t('logUpdate'));
  const copies = update ? [] : game;
  const fixed = [{ path: `${ROOT}/mini-ninjas.nro`, bytes: release.nro },
    ...release.runtime.map((f) => ({ path: `${ROOT}/${f.path}`, bytes: f.bytes }))];
  onSize(zipSize([...fixed.map((f) => ({ path: f.path, size: f.bytes.length })),
    ...copies.map((f) => ({ path: `${ROOT}/game/${f.path}`, size: f.size }))]));
  const zip = new ZipWriter(sink);
  for (const f of fixed) await zip.addBytes(f.path, f.bytes);
  const total = copies.reduce((sum, f) => sum + f.size, 0);
  let done = 0;
  for (const f of copies) {
    await zip.addFile(`${ROOT}/game/${f.path}`, f.size, chunksOf(f), (chunk) => {
      done += chunk.length; progress(total ? done / total : 1);
    });
    log(t('copied', { path: f.path }));
  }
  await zip.finish(); progress(1); log(t('done'));
  return { edition: build.edition, files: copies.length };
}
