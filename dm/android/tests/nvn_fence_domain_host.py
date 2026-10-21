#!/usr/bin/env python3
"""Exercise actual NVN fence APIs with distinct logical/GPU ticket domains."""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from native_callbacks_host import extract_function

PRELUDE = r'''
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <assert.h>
#define __ANDROID__ 1
#define NVN(name,ret,...) ret name(__VA_ARGS__)
typedef struct {int fenced;uint64_t fence;} sync_t;typedef sync_t NVNsync;typedef int NVNqueue;typedef int NVNsyncCondition;typedef int NVNsyncWaitResult;
enum {NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED=0,NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED=1,NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED=2};
static _Atomic uint64_t g_x_subs=5;static uint64_t gpu_encoded=5,gpu_completed=5,seen_timeout;static int drains;
static void x_drain(void){drains++;gpu_encoded=7;}
static uint64_t mtl_last_fence(void){return gpu_encoded;}
static int mtl_wait(uint64_t ticket,uint64_t timeout){seen_timeout=timeout;if(gpu_completed>=ticket)return 1;if(timeout==42){gpu_completed=7;return 1;}return 0;}
'''
SCENARIO = r'''
int main(void){NVNsync s={0};assert(nvnSyncWait(&s,0)==0);nvnQueueFenceSync(0,&s,0,0);
if(s.fence!=7){fprintf(stderr,"FAIL logical ordinal %llu mistaken for GPU ticket7: old completion5 would release pending work\n",(unsigned long long)s.fence);return 1;}
assert(drains==1);assert(nvnSyncWait(&s,0)==NVN_SYNC_WAIT_RESULT_TIMEOUT_EXPIRED);assert(gpu_completed==5);assert(nvnSyncWait(&s,42)==NVN_SYNC_WAIT_RESULT_CONDITION_SATISFIED);assert(seen_timeout==42);assert(nvnSyncWait(&s,0)==NVN_SYNC_WAIT_RESULT_ALREADY_SIGNALED);puts("PASS actual FenceSync/x_fence/SyncWait: ordinal5 maps GPU7, completion5 cannot release, timeout forwarded, real7 releases");}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    source = (Path(__file__).resolve().parents[2] / "gfx/nvn/hle_nvn.c").read_text()
    source = re.sub(
        r"(?m)^NVN\((nvnQueueFenceSync|nvnSyncWait), ([^,]+), (.*?)\) \{",
        r"static \2 \1(\3) {", source,
    )
    fence = extract_function(source[source.index("static uint64_t x_fence(void) {"):], "x_fence")
    apis = "\n".join(extract_function(source, name) for name in ("nvnQueueFenceSync", "nvnSyncWait"))
    mutant = "static uint64_t x_fence(void) { return atomic_load(&g_x_subs); }"
    with tempfile.TemporaryDirectory(prefix="nvn-fence-domain-") as directory:
        root = Path(directory)
        for name, body, expected_success in (("production", fence, True), ("ordinal-mutant", mutant, False)):
            path = root / (name + ".c")
            path.write_text(PRELUDE + body + "\n" + apis + "\n" + SCENARIO)
            binary = root / name
            subprocess.run([args.cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-variable", "-Wno-unused-function", str(path), "-o", str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
            if (result.returncode == 0) != expected_success:
                raise SystemExit(f"{name} unexpected exit {result.returncode}: {result.stdout}{result.stderr}")
            print(name + ": " + ("PASS" if expected_success else "rejected early completion"))


if __name__ == "__main__":
    main()
