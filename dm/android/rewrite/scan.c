// 本地 NSO 审计／内存准备检查；依据 prepare.h，不写游戏文件、不映射或执行客体代码。
// Local NSO audit / in-memory preparation check; based on prepare.h, never write game files, map or execute guest code.
#include "prepare.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc,char**argv){
    int exporting=argc>1&&!strcmp(argv[1],"--export");
    int prepare=exporting||(argc>1&&!strcmp(argv[1],"--prepare-check")),first=exporting?3:prepare?2:1;
    if(argc<=first||(exporting&&argc!=4)){fprintf(stderr,"Usage: %s [--prepare-check] <ExeFS/module> [more modules]\n       %s --export <new-output.dma> <ExeFS/module>\n",argv[0],argv[0]);return 2;}
    long page=sysconf(_SC_PAGESIZE);if(page<8||(page&(page-1)))return 1;
    size_t alignment=(size_t)page<16384?16384:(size_t)page;
    int status=0;
    for(int file=first;file<argc;file++){
        dm_nso_image image={0};dm_nso_result read=dm_nso_read(argv[file],&image);
        if(read!=DM_NSO_OK){fprintf(stderr,"%s: %s\n",argv[file],dm_nso_error(read));return 1;}
        size_t stub=(image.header.image_size+alignment-1)&~(alignment-1);
        dm_rewrite_report r={0};dm_rewrite_output output={0};
        int result=dm_rewrite_prepare(&image,(uint32_t)stub,1,&r,prepare?&output:NULL);
        printf("%s: system=%zu x18_plain=%zu x18_pcrel=%zu x18_conditional=%zu x18_jump=%zu exclusive_windows=%zu rewritten_windows=%zu rewritten_window_sites=%zu window_sites=%zu unmatched_loads=%zu "
            "deferred_pcrel=%zu deferred_branch=%zu deferred_other=%zu undecodable_words=%zu emitter_errors=%zu unsupported_system=%zu stub_bytes=%zu sites=%zu native_exclusive=%zu metadata_words=%zu metadata_entries=%zu retained_traps=%zu literal_words=%zu literal_conflicts=%zu\n",
            argv[file],r.systems,r.plain,r.addresses,r.conditionals,r.jumps,r.windows,r.rewritten_windows,r.rewritten_sites,r.window_sites,
            r.unmatched,r.pcrel,r.branches,r.other,r.undecodable,r.emitter_errors,r.unsupported_system,r.stub_bytes,r.sites,r.native_exclusive,r.metadata_words,r.metadata_entries,r.retained_traps,r.literal_words,r.literal_conflicts);
        if(!result&&exporting)result=dm_artifact_write(argv[2],&output);
        dm_rewrite_free(&output);dm_nso_free(&image);
        if(result==ENOTSUP)status=3;
        else if(result){fprintf(stderr,"%s: rewrite failed: %s\n",argv[file],strerror(result));return 1;}
    }
    puts(exporting?"Artifact export: only covered input produces a new file with cleared gate addresses. No mappings, initializers or guest execution.":
        prepare?"Preparation check only: discard in-memory output; gate address is a placeholder. No file writes, mappings or guest execution.":
        "Audit only: no game file modifications, executable output or guest execution. Accepted exclusive windows use whole-region rewriting.");
    return status;
}
