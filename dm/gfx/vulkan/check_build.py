"""Compile/link Android graphics without game resources, device access or full app."""
import argparse, pathlib, subprocess, hashlib, json
ap=argparse.ArgumentParser();ap.add_argument('--ndk',type=pathlib.Path,required=True);ap.add_argument('--output',type=pathlib.Path,required=True);args=ap.parse_args()
dm=pathlib.Path(__file__).resolve().parents[2];root=dm/'gfx/vulkan';out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
tool=args.ndk/'toolchains/llvm/prebuilt/windows-x86_64';common=['--target=aarch64-linux-android29','--sysroot='+str(tool/'sysroot'),'-fPIC','-O1','-fsigned-char','-I'+str(dm/'gfx/nvn/include'),'-I'+str(dm/'host')]
log=[];objects=[]
def run(command):
 r=subprocess.run(command,capture_output=True,text=True,encoding='utf-8',errors='replace');log.append({'command':command,'exit':r.returncode,'stdout':r.stdout,'stderr':r.stderr});print(pathlib.Path(command[0]).name,'exit',r.returncode,flush=True)
 if r.returncode:
  (out/'validation.json').write_text(json.dumps(log,indent=2),encoding='utf-8');raise SystemExit(r.stderr)
files=sorted((root/'engine').glob('*.c'))+sorted((root/'engine').glob('*.cpp'))+[root/'nvn_vk.c',dm/'android/vulkan_present.cpp',dm/'gfx/nvn/nvn_tex.c',dm/'gfx/tex/nvn_deswizzle.c']
for p in files:
 obj=out/(p.stem+'.o');opts=['-std=c++17'] if p.suffix=='.cpp' else []
 if p.name=='nvn_vk.c':opts+=['-I'+str(dm/'android/compat/include'),'-include',str(dm/'android/compat/include/dm_android_compat.h')]
 run([str(tool/'bin/clang.exe'),*common,*opts,'-c',str(p),'-o',str(obj)]);objects.append(obj)
# Only external host dependencies: diagnostics + hle_nvn serial generator.
# This confirms graphics linkage, not full host linkage or execution semantics.
stub=out/'link_host_stubs.c';stub.write_text('#include <stdint.h>\n#include <stdlib.h>\nvoid log_(const char*f,...){(void)f;}\nvoid die(const char*f,...){(void)f;abort();}\nuint64_t nvn_new_serial(void){static uint64_t n;return ++n;}\n',encoding='utf-8');obj=out/'link_host_stubs.o';run([str(tool/'bin/clang.exe'),*common,'-c',str(stub),'-o',str(obj)]);objects.append(obj)
lib=out/'libdm_vulkan_link_check.so';run([str(tool/'bin/clang++.exe'),*common,'-shared','-Wl,--no-undefined','-o',str(lib),*[str(p) for p in objects],'-lvulkan','-landroid','-llog'])
(out/'validation.json').write_text(json.dumps({'checks':log,'artifact':str(lib),'sha256':hashlib.sha256(lib.read_bytes()).hexdigest()},indent=2),encoding='utf-8');print('graphics-only link OK; no device validation')
