// 小端有界产物及宿主门重绑定；依据 artifact.h / SHA-256 / POSIX，摘要不替代来源信任，绝不自动跳入代码。
// Bounded little-endian artifacts and host-gate rebinding; based on artifact.h / SHA-256 / POSIX, checksums do not establish trust and never imply automatic execution.
#include "artifact.h"
#include <errno.h>
#include <fcntl.h>
#include <mbedtls/sha256.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define HEADER 160u
#define ALIGNMENT 16384u
static const uint8_t magic[8]={'D','M','A','R','6','4',0,0};
static uint32_t get(const uint8_t*p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint8_t*p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static int validate(const dm_rewrite_output*a){
    if(!a||!a->image.bytes||!a->image.header.image_size||a->image.header.image_size>DM_NSO_MAX_IMAGE||
        a->count>DM_NSO_MAX_IMAGE/32||(a->count&&(!a->gate_literals||!a->sites)))return EINVAL;
    const dm_nso_header*h=&a->image.header;
    uint64_t lo[3],hi[3];
    for(unsigned i=0;i<3;i++){
        lo[i]=h->segments[i].memory_offset;hi[i]=lo[i]+h->segments[i].size+(i==2?(uint64_t)h->bss_size:0);
        if(hi[i]>h->image_size)return EINVAL;
        for(unsigned j=0;j<i;j++)if(hi[i]>lo[i]&&hi[j]>lo[j]&&lo[i]<hi[j]&&lo[j]<hi[i])return EINVAL;
    }
    if(h->segments[0].memory_offset%4||h->segments[0].size%4)return EINVAL;
    if(hi[0]>lo[0]&&hi[2]>lo[2]&&lo[0]/ALIGNMENT<=(hi[2]-1)/ALIGNMENT&&lo[2]/ALIGNMENT<=(hi[0]-1)/ALIGNMENT)return EINVAL;
    size_t end=(h->image_size+ALIGNMENT-1)&~(size_t)(ALIGNMENT-1);
    if(a->code.size){
        if(!a->code.bytes||!a->count||a->code.offset<end||a->code.offset%ALIGNMENT||a->code.offset>DM_NSO_MAX_IMAGE||
            a->code.size>DM_NSO_MAX_IMAGE-a->code.offset)return EINVAL;
    }else if(a->code.offset||a->code.bytes||a->count)return EINVAL;
    dm_gate_table table={a->sites,a->count};if(dm_gate_table_validate(&table))return EINVAL;
    for(size_t i=0;i<a->count;i++){
        uintptr_t pc=a->sites[i].return_address;size_t literal=a->gate_literals[i];
        if(pc%4||pc<a->code.offset||pc-a->code.offset>=a->code.size||literal%8||a->code.size<8||
            literal>a->code.size-8||literal<=pc-a->code.offset||(i&&literal<a->gate_literals[i-1]+8u))return EINVAL;
    }
    return 0;
}
void dm_rewrite_free(dm_rewrite_output*out){
    if(!out)return;dm_nso_free(&out->image);free((void*)out->code.bytes);free(out->sites);free(out->gate_literals);memset(out,0,sizeof(*out));
}
static int digest(const uint8_t*bytes,size_t size,uint8_t hash[32]){
    uint8_t header[HEADER];memcpy(header,bytes,HEADER);memset(header+96,0,32);
    mbedtls_sha256_context context;mbedtls_sha256_init(&context);
    int rc=mbedtls_sha256_starts(&context,0);
    if(!rc)rc=mbedtls_sha256_update(&context,header,HEADER);
    if(!rc)rc=mbedtls_sha256_update(&context,bytes+HEADER,size-HEADER);
    if(!rc)rc=mbedtls_sha256_finish(&context,hash);
    mbedtls_sha256_free(&context);return rc?EIO:0;
}
int dm_artifact_encode(const dm_rewrite_output*a,uint8_t**bytes,size_t*size){
    if(!bytes||!size||validate(a))return EINVAL;
    uint64_t total=HEADER+(uint64_t)a->image.header.image_size+a->code.size+16ull*a->count;
    if(total>DM_NSO_MAX_IMAGE)return EOVERFLOW;
    uint8_t*p=calloc(1,(size_t)total);if(!p)return ENOMEM;
    memcpy(p,magic,8);put(p+8,1);put(p+12,HEADER);put(p+16,(uint32_t)a->image.header.image_size);
    put(p+20,a->image.header.bss_size);put(p+24,(uint32_t)a->code.offset);put(p+28,(uint32_t)a->code.size);put(p+32,(uint32_t)a->count);
    for(unsigned i=0;i<3;i++){put(p+40+8*i,a->image.header.segments[i].memory_offset);put(p+44+8*i,a->image.header.segments[i].size);}
    memcpy(p+64,a->image.header.build_id,32);memcpy(p+HEADER,a->image.bytes,a->image.header.image_size);
    size_t stub=HEADER+a->image.header.image_size,records=stub+a->code.size;
    if(a->code.size)memcpy(p+stub,a->code.bytes,a->code.size);
    for(size_t i=0;i<a->count;i++){
        uint8_t*r=p+records+16*i;put(r,(uint32_t)a->sites[i].return_address);put(r+4,(uint32_t)a->sites[i].kind);
        put(r+8,a->sites[i].argument);put(r+12,a->gate_literals[i]);memset(p+stub+a->gate_literals[i],0,8);
    }
    uint8_t hash[32];int rc=digest(p,(size_t)total,hash);
    if(rc){free(p);return rc;}memcpy(p+96,hash,32);*bytes=p;*size=(size_t)total;return 0;
}
int dm_artifact_decode(const void*bytes,size_t size,dm_rewrite_output*out){
    if(!bytes||!out||size<HEADER||size>DM_NSO_MAX_IMAGE)return EINVAL;
    const uint8_t*p=bytes;if(memcmp(p,magic,8)||get(p+12)!=HEADER)return EINVAL;if(get(p+8)!=1)return ENOTSUP;
    for(unsigned i=36;i<40;i++)if(p[i])return EINVAL;for(unsigned i=128;i<HEADER;i++)if(p[i])return EINVAL;
    size_t image=get(p+16),code=get(p+28),count=get(p+32);
    if(!image||count>DM_NSO_MAX_IMAGE/32||HEADER+(uint64_t)image+code+16ull*count!=size)return EINVAL;
    uint8_t hash[32];int result=digest(p,size,hash);if(result)return result;if(memcmp(hash,p+96,32))return EBADMSG;
    dm_rewrite_output a={.image.header={.image_size=image,.bss_size=get(p+20)},.code={.offset=get(p+24),.size=code},.count=count};
    for(unsigned i=0;i<3;i++){a.image.header.segments[i].memory_offset=get(p+40+8*i);a.image.header.segments[i].size=get(p+44+8*i);}
    memcpy(a.image.header.build_id,p+64,32);a.image.bytes=malloc(image);
    if(code)a.code.bytes=malloc(code);
    if(count){a.sites=calloc(count,sizeof(*a.sites));a.gate_literals=calloc(count,sizeof(*a.gate_literals));}
    result=ENOMEM;if(!a.image.bytes||(code&&!a.code.bytes)||(count&&(!a.sites||!a.gate_literals)))goto done;
    memcpy(a.image.bytes,p+HEADER,image);if(code)memcpy((void*)a.code.bytes,p+HEADER+image,code);
    for(size_t i=0;i<count;i++){
        const uint8_t*r=p+HEADER+image+code+16*i;
        a.sites[i]=(dm_gate_site){.return_address=get(r),.kind=(dm_gate_kind)get(r+4),.argument=get(r+8)};a.gate_literals[i]=get(r+12);
    }
    result=validate(&a);if(result)goto done;
    for(size_t i=0;i<count;i++)for(unsigned j=0;j<8;j++)if(a.code.bytes[a.gate_literals[i]+j]){result=EINVAL;goto done;}
    *out=a;memset(&a,0,sizeof(a));
done:dm_rewrite_free(&a);return result;
}
int dm_artifact_read(const char*path,dm_rewrite_output*out){
    if(!path||!out)return EINVAL;FILE*f=fopen(path,"rb");if(!f)return errno;
    int rc=EIO;uint8_t*p=NULL;long size=0;
    if(fseek(f,0,SEEK_END)||(size=ftell(f))<HEADER||(unsigned long)size>DM_NSO_MAX_IMAGE||fseek(f,0,SEEK_SET))goto done;
    p=malloc((size_t)size);if(!p){rc=ENOMEM;goto done;}
    if(fread(p,1,(size_t)size,f)!=(size_t)size||fgetc(f)!=EOF||ferror(f))goto done;
    rc=0;
done:if(fclose(f)&&!rc)rc=EIO;if(!rc)rc=dm_artifact_decode(p,(size_t)size,out);free(p);return rc;
}
int dm_artifact_write(const char*path,const dm_rewrite_output*input){
    if(!path)return EINVAL;uint8_t*p=NULL;size_t size=0;int rc=dm_artifact_encode(input,&p,&size);if(rc)return rc;
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0){rc=errno;free(p);return rc;}
    size_t at=0;while(at<size){ssize_t n=write(fd,p+at,size-at);if(n<0&&errno==EINTR)continue;
        if(n<=0){rc=n<0?errno:EIO;break;}at+=(size_t)n;}
    if(close(fd)&&!rc)rc=errno;if(rc)unlink(path);free(p);return rc;
}
static int compare(const void*a,const void*b){uintptr_t x=((const dm_gate_site*)a)->return_address,y=((const dm_gate_site*)b)->return_address;return(x>y)-(x<y);}
int dm_artifacts_unmap(dm_artifact_mappings*m){
    if(!m)return EINVAL;if(dm_modules_unmap(&m->modules))return EIO;free(m->sites);memset(m,0,sizeof(*m));return 0;
}
int dm_artifacts_map(const dm_rewrite_output*inputs,size_t count,uintptr_t host_gate,dm_artifact_mappings*out){
    return dm_artifacts_map_imports(inputs,count,host_gate,NULL,0,out);
}
int dm_artifacts_map_imports(const dm_rewrite_output*inputs,size_t count,uintptr_t host_gate,
    const dm_symbol_override*imports,size_t import_count,dm_artifact_mappings*out){
    if(!inputs||!out||!count||count>16||!host_gate)return EINVAL;
    dm_nso_image images[16]={0};dm_image_code codes[16]={0};dm_artifact_mappings m={0};int rc=EINVAL;size_t total=0;
    for(size_t i=0;i<count;i++){
        if(validate(&inputs[i])||inputs[i].count>DM_NSO_MAX_IMAGE/sizeof(dm_gate_site)-total)goto done;
        total+=inputs[i].count;images[i]=inputs[i].image;codes[i]=inputs[i].code;
        codes[i].bytes=NULL;
        if(codes[i].size){uint8_t*p=malloc(codes[i].size);if(!p){rc=ENOMEM;goto done;}
            memcpy(p,inputs[i].code.bytes,codes[i].size);codes[i].bytes=p;
            for(size_t j=0;j<inputs[i].count;j++)for(unsigned b=0;b<8;b++)p[inputs[i].gate_literals[j]+b]=(uint8_t)((uint64_t)host_gate>>(8*b));}
    }
    m.sites=total?calloc(total,sizeof(*m.sites)):NULL;if(total&&!m.sites){rc=ENOMEM;goto done;}
    dm_module_result mapped=dm_modules_map_imports(images,codes,count,imports,import_count,&m.modules);
    if(mapped!=DM_MODULE_OK){rc=mapped==DM_MODULE_UNRESOLVED?ENOENT:EIO;goto done;}
    size_t at=0;for(size_t i=0;i<count;i++)for(size_t j=0;j<inputs[i].count;j++){
        m.sites[at]=inputs[i].sites[j];m.sites[at++].return_address+=(uintptr_t)m.modules.mappings[i].base;}
    if(total)qsort(m.sites,total,sizeof(*m.sites),compare);m.gates=(dm_gate_table){m.sites,total};
    rc=dm_gate_table_validate(&m.gates);if(rc)goto done;*out=m;memset(&m,0,sizeof(m));
done:for(size_t i=0;i<count;i++)free((void*)codes[i].bytes);dm_artifacts_unmap(&m);return rc;
}
