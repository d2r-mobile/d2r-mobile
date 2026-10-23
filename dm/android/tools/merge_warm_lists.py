#!/usr/bin/env python3
"""合并几台设备的管线预热列表（files/dm/vk-pipeline-list.bin），让每台都在启动时建好别的设备见过的管线。
记录格式同引擎（xbg_game.cpp warm::start）：魔数 0x57504c58、阶段数（≤5）、各阶段着色器键（长度 ≤256）、建管线信息键（≤4096 字）。
按建管线信息键去重，保留第一份的顺序；坏记录及其后的内容同引擎一样丢掉。设备上不认得的记录在预热时建失败（warm_failed），不影响游戏。
Merge the pipeline warm-up lists (files/dm/vk-pipeline-list.bin) of several devices, so that each builds at start-up the
pipelines the others have seen. Records as in the engine (xbg_game.cpp warm::start): magic 0x57504c58, stage count (<= 5),
each stage's shader key (length <= 256), the create-info key (<= 4096 words). Records are de-duplicated by the create-info
key, keeping the first file's order; a bad record and everything after it is dropped, as the engine does. A record a
device cannot build fails in its warm-up (warm_failed) without affecting the game.
"""
import argparse
import struct
import sys

MAGIC = 0x57504C58
MAX_STAGES = 5
MAX_KEY = 256
MAX_WORDS = 4096


def records(data):
    """每条好记录的（建管线信息键字节, 记录字节），到第一条坏记录为止 | (create-info key bytes, record bytes) for each good record, stopping at the first bad one."""
    i, out = 0, []

    def get():
        nonlocal i
        if i + 4 > len(data):
            return None
        v = struct.unpack_from('<I', data, i)[0]
        i += 4
        return v

    while i < len(data):
        start = i
        magic, ns = get(), get()
        if magic != MAGIC or ns is None or ns > MAX_STAGES:
            break
        good = True
        for _ in range(ns):
            n = get()
            if n is None or n > MAX_KEY or i + n > len(data):
                good = False
                break
            i += n
        count = get() if good else None
        if count is None or count > MAX_WORDS or i + count * 4 > len(data):
            break
        key = data[i:i + count * 4]
        i += count * 4
        out.append((key, data[start:i]))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('lists', nargs='+', help='vk-pipeline-list.bin files, first one first')
    ap.add_argument('-o', '--output', required=True)
    a = ap.parse_args()
    seen, merged = set(), []
    for path in a.lists:
        recs = records(open(path, 'rb').read())
        new = [r for k, r in recs if k not in seen and not seen.add(k)]
        merged += new
        print('%s: %d records, %d new' % (path, len(recs), len(new)))
    with open(a.output, 'wb') as f:
        f.write(b''.join(merged))
    print('%s: %d records' % (a.output, len(merged)))


if __name__ == '__main__':
    sys.exit(main())
