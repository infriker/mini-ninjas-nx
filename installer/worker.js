// Builds the package off the main thread and streams the zip to the download
// service worker. Adapted from nfsmw-nx-installer by StevensND (GPL-3.0).
import { listIsoFiles } from './lib/iso.js';
import { createPackage, identifyGame, programPath, runtimePath, rootAtExecutable, sha256 } from './lib/installer.js';
import { t, setLanguage } from './lib/i18n.js';

const log = (text) => postMessage({ type: 'log', text });
const progress = (fraction) => postMessage({ type: 'progress', fraction });

async function fetchBytes(url) {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error(t('downloadFailed', { url, status: response.status }));
  }
  return new Uint8Array(await response.arrayBuffer());
}

// Sends every write as one chunk, and only when the download has asked for
// more ("pull"), so memory stays bounded however big the zip is.
function portSink(port) {
  let credits = 0;
  let cancelled = false;
  let waiting = null;
  port.onmessage = ({ data }) => {
    if (data.type === 'pull') {
      credits++;
    } else if (data.type === 'cancel') {
      cancelled = true;
    }
    if (waiting) {
      const wake = waiting;
      waiting = null;
      wake();
    }
  };
  return {
    async write(bytes) {
      while (!credits) {
        if (cancelled) {
          throw new Error(t('cancelled'));
        }
        await new Promise((resolve) => (waiting = resolve));
      }
      if (cancelled) {
        throw new Error(t('cancelled'));
      }
      credits--;
      const copy = bytes.slice();
      port.postMessage({ type: 'chunk', buffer: copy.buffer }, [copy.buffer]);
    },
    async close() {
      port.postMessage({ type: 'end' });
    },
    fail(message) {
      port.postMessage({ type: 'error', message });
    },
  };
}

async function gameFiles(data) {
  const files = data.kind === 'iso'
    ? await listIsoFiles(data.file)
    : data.entries.map(({ path, file }) => ({
        path,
        size: file.size,
        read: async (offset, length) => new Uint8Array(await file.slice(offset, offset + length).arrayBuffer()),
      }));
  return rootAtExecutable(files);
}

self.onmessage = async ({ data }) => {
  setLanguage(data.language);
  const sink = portSink(data.port);
  try {
    log(t('readingFiles'));
    const files = await gameFiles(data);
    const manifest = JSON.parse(new TextDecoder().decode(await fetchBytes('./release/manifest.json')));
    const { build, xexHash } = await identifyGame(files, manifest);
    if (!build) {
      throw new Error(t('notSupported', { hash: xexHash }));
    }
    log(t('downloadingBuild'));
    if (!Array.isArray(build.runtime_files) || !build.runtime_files.some((f) => f.path === 'minininjas_shaders.dslib')) {
      throw new Error(t('buildMismatch'));
    }
    const seen = new Set();
    for (const file of build.runtime_files) {
      if (typeof file.path !== 'string' || file.path.startsWith('/') || file.path.includes('\\') ||
          file.path.split('/').some((part) => !part || part === '.' || part === '..') ||
          file.path.toLowerCase() === 'mini-ninjas.nro' || file.path.toLowerCase().startsWith('game/') ||
          seen.has(file.path.toLowerCase()) || !/^[0-9a-f]{64}$/.test(file.sha256) ||
          !Number.isSafeInteger(file.size) || file.size < 0 || file.size > 128 * 1024 * 1024) {
        throw new Error(t('buildMismatch'));
      }
      seen.add(file.path.toLowerCase());
    }
    const [nro, ...bytes] = await Promise.all([
      fetchBytes(programPath(build)), ...build.runtime_files.map((f) => fetchBytes(runtimePath(f))),
    ]);
    if ((await sha256(nro)) !== build.nro_sha256) {
      throw new Error(t('buildMismatch'));
    }
    const runtime = [];
    for (let i = 0; i < bytes.length; i++) {
      const file = build.runtime_files[i];
      if (bytes[i].length !== file.size || (await sha256(bytes[i])) !== file.sha256) throw new Error(t('buildMismatch'));
      runtime.push({ path: file.path, bytes: bytes[i] });
    }
    const result = await createPackage(files, { manifest, nro, runtime }, sink, log, progress,
      (size) => postMessage({ type: 'size', size }), { update: Boolean(data.update) });
    postMessage({ type: 'done', result });
  } catch (error) {
    const message = error.message || String(error);
    sink.fail(message);
    postMessage({ type: 'error', message });
  }
};
