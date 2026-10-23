import { fileIo } from '@kit.CoreFileKit';

// 只读的英雄摘要（D2R .d2s，版本 0x61 起）：职业 @0x18、等级 @0x1B、上次游玩 unix 秒 @0x20、UTF-8 名字 @0x12B；金币取「gf」属性段
// （9 位属性号、低位在前的位域）。存档在 files/save/<名字>/<名字>.d2s。只读文件头，不碰游戏数据表。
// Read-only hero summary (D2R .d2s, version 0x61+): class @0x18, level @0x1B, last-played unix seconds @0x20, UTF-8 name @0x12B; gold from the
// "gf" stats section (9-bit stat id, LSB-first bit fields). Saves live in files/save/<name>/<name>.d2s. Only the file header is read; no game data table is used.
export interface SaveSummary {
  name: string; classIndex: number; level: number; lastPlayed: number; gold: number;
}

// 属性 0..15 的位宽（存档格式本身的字段宽度）| Bit widths of stats 0..15 (field widths of the save format itself)
const WIDTH: number[] = [10, 10, 10, 10, 10, 8, 21, 21, 21, 21, 21, 21, 7, 32, 25, 25];

function stats(bytes: Uint8Array, start: number): Map<number, number> {
  const out = new Map<number, number>(); let pos = 0;
  const read = (n: number): number => {
    let v = 0;
    for (let i = 0; i < n; i++, pos++) { const b = start + (pos >> 3); if (b >= bytes.length) { return -1; } if ((bytes[b] >> (pos & 7)) & 1) { v += Math.pow(2, i); } }
    return v;
  };
  for (let k = 0; k < 32; k++) {
    const id = read(9); if (id < 0 || id === 0x1ff || id >= WIDTH.length) { break; }
    const v = read(WIDTH[id]); if (v < 0) { break; } out.set(id, v);
  }
  return out;
}

function readSave(path: string): SaveSummary | undefined {
  let file: fileIo.File | undefined = undefined;
  try {
    file = fileIo.openSync(path, fileIo.OpenMode.READ_ONLY);
    const buf = new ArrayBuffer(2048); const n = fileIo.readSync(file.fd, buf);
    if (n < 0x15b) { return undefined; }
    const v = new DataView(buf); const bytes = new Uint8Array(buf, 0, n);
    if (v.getUint32(0, true) !== 0xaa55aa55) { return undefined; }
    const nb = new Uint8Array(buf, 0x12b, 0x30);
    let end = nb.indexOf(0); if (end < 0) { end = nb.length; }
    let name = ''; for (let i = 0; i < end; i++) { name += String.fromCharCode(nb[i]); }
    try { name = decodeURIComponent(escape(name)); } catch (e) { /* 保留原字节 | keep latin1 */ }
    let level = v.getUint8(0x1b), gold = 0;
    for (let i = 0x100; i + 1 < n; i++) {
      if (bytes[i] === 0x67 && bytes[i + 1] === 0x66) {   // "gf"
        const st = stats(bytes, i + 2); gold = (st.get(14) ?? 0) + (st.get(15) ?? 0);
        if (st.has(12)) { level = st.get(12) ?? level; }
        break;
      }
    }
    const cls = v.getUint8(0x18);
    return { name: name, classIndex: cls < 8 ? cls : -1, level: level, lastPlayed: v.getUint32(0x20, true), gold: gold };
  } catch (e) { return undefined; }
  finally { if (file) { fileIo.closeSync(file); } }
}

// 全部英雄，最近玩的在前 | All heroes, most recently played first
export function allSaves(filesDir: string): SaveSummary[] {
  const root = filesDir + '/save'; const out: SaveSummary[] = [];
  try {
    for (const dir of fileIo.listFileSync(root)) {
      const path = `${root}/${dir}/${dir}.d2s`;
      try { if (!fileIo.accessSync(path)) { continue; } } catch (e) { continue; }
      const s = readSave(path); if (s) { out.push(s); }
    }
  } catch (e) { return out; }
  out.sort((a: SaveSummary, b: SaveSummary) => b.lastPlayed - a.lastPlayed);
  return out;
}
