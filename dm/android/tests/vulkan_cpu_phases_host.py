"""Check actual phase-timing code with a counted clock and Android property shim."""
from pathlib import Path
import subprocess
import sys

source = Path(sys.argv[1]).read_text()
end = source.index('// 异常帧独立日志')
production = source[:end]
# Permit a red run against the original always-on implementation.
if 'static void dm_cpu_phase_configure(' not in production:
    production += '\nstatic void dm_cpu_phase_configure(void) {}\n'
prefix = r'''
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define __ANDROID__ 1
#define PROP_VALUE_MAX 92
static char property[PROP_VALUE_MAX];
static unsigned clock_calls, log_calls;
static int diagnostics=1;
static int dm_diagnostics_enabled(void) {return diagnostics;}
static uint64_t dm_android_clock_ns(clockid_t clock) {
    (void)clock;return (++clock_calls)*100;
}
static __attribute__((unused)) int __system_property_get(const char *key,char *value) {
    assert(!strcmp(key,"debug.d2r.cpu_phases"));strcpy(value,property);return (int)strlen(value);
}
#define LOG(...) do {log_calls++;} while(0)
'''
body = r'''
int main(void) {
    unsetenv("DM_VK_CPU_PHASES");property[0]=0;dm_cpu_phase_configure();
    unsigned previous_logs=log_calls;
    for(unsigned i=0;i<10000;i++)dm_cpu_phase_end(DM_CPU_PHASE_DRAW,dm_cpu_phase_begin());
    dm_cpu_phase_report(1);
    assert(clock_calls==0 && log_calls==previous_logs && dm_cpu_phase_calls[DM_CPU_PHASE_DRAW]==0);
    setenv("DM_VK_CPU_PHASES","1",1);dm_cpu_phase_configure();
    dm_cpu_phase_end(DM_CPU_PHASE_DRAW,dm_cpu_phase_begin());
    assert(clock_calls==2 && dm_cpu_phase_calls[DM_CPU_PHASE_DRAW]==1 && dm_cpu_phase_ns[DM_CPU_PHASE_DRAW]==100);
    previous_logs=log_calls;dm_cpu_phase_report(1);assert(log_calls==previous_logs+1);
    strcpy(property,"0");dm_cpu_phase_configure();
    unsigned previous_clock=clock_calls;
    dm_cpu_phase_end(DM_CPU_PHASE_DRAW,dm_cpu_phase_begin());assert(clock_calls==previous_clock);
    setenv("DM_VK_CPU_PHASES","invalid",1);property[0]=0;dm_cpu_phase_configure();
    assert(dm_cpu_phase_begin()==0);
    strcpy(property,"1");dm_cpu_phase_configure();assert(dm_cpu_phase_begin()!=0);
    diagnostics=0;unsigned before=clock_calls;dm_cpu_phase_configure();
    dm_cpu_phase_end(DM_CPU_PHASE_DRAW,dm_cpu_phase_begin());assert(clock_calls==before);
    puts("actual phase timing: default zero clock calls, opt-in timings, strict property override PASS");
}
'''
output = Path(sys.argv[2])
output.write_text(prefix + production + body)
subprocess.run([sys.argv[3] if len(sys.argv)>3 else 'clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(Path(sys.argv[1]).parent),str(output),'-o',str(output.with_suffix(''))], check=True)
subprocess.run([str(output.with_suffix(''))], check=True)
