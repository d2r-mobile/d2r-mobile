// ARM64 产物检查器；依据 artifact.h，仅检查／可选装载，不执行入口或初始化器。
// ARM64 artifact inspector; based on artifact.h, inspect / optionally map without executing entries or initializers.
#include "artifact.h"
#include <stdio.h>
#include <string.h>
int main(int argc,char**argv){
    const char*program=argv[0];int map=argc>1&&!strcmp(argv[1],"--map");if(map){argc--;argv++;}
    if(argc<2||argc>17){fprintf(stderr,"Usage: %s [--map] <artifact> [more artifacts, max 16]\n",program);return 2;}
    dm_rewrite_output inputs[16]={0};dm_artifact_mappings mapped={0};int status=1;
    for(int i=1;i<argc;i++){
        int rc=dm_artifact_read(argv[i],&inputs[i-1]);if(rc){fprintf(stderr,"%s: %s\n",argv[i],strerror(rc));goto done;}
        printf("%s: image=%zu stub_offset=%zu stub_bytes=%zu gates=%zu checksum=OK\n",argv[i],inputs[i-1].image.header.image_size,
            inputs[i-1].code.offset,inputs[i-1].code.size,inputs[i-1].count);
    }
    if(map){
#if defined(__aarch64__)
        extern void dm_gate(void);int rc=dm_artifacts_map(inputs,(size_t)argc-1,(uintptr_t)dm_gate,&mapped);
        if(rc){fprintf(stderr,"Mapping: %s\n",strerror(rc));goto done;}
        printf("Mapped %zu modules; relocations=%zu rebound_gates=%zu\n",mapped.modules.count,mapped.modules.relocations,mapped.gates.count);
#else
        fputs("Mapping requires an ARM64 host gate\n",stderr);goto done;
#endif
    }
    puts("Inspection only: no entries, initializers or guest instructions executed.");status=0;
done:
    if(dm_artifacts_unmap(&mapped))status=1;
    for(int i=1;i<argc;i++)dm_rewrite_free(&inputs[i-1]);return status;
}
