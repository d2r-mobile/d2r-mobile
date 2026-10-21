// Bound staging retention and command size; each batch retires with its GPU submission.
#define TEXTURE_UPLOAD_REGIONS 64
#define TEXTURE_UPLOAD_BYTES (8ull << 20)
static int texture_upload_batch = 1;
static uint64_t texture_upload_copy_calls, texture_upload_subresources, texture_upload_bytes;
static void texture_upload_report(uint64_t ticket) {
    if(ticket%60)return;
#ifdef __ANDROID__
    char value[PROP_VALUE_MAX]={0};__system_property_get("debug.d2r.upload_batch",value);
    if(!strcmp(value,"0"))texture_upload_batch=0;
    else if(!strcmp(value,"1")||!value[0])texture_upload_batch=1;
#endif
    static uint64_t calls,subresources,bytes;
    LOG("[vulkan-upload-batch] frame=%llu enabled=%d copy_calls=%llu subresources=%llu bytes=%llu",
        (unsigned long long)ticket,texture_upload_batch,(unsigned long long)(texture_upload_copy_calls-calls),
        (unsigned long long)(texture_upload_subresources-subresources),(unsigned long long)(texture_upload_bytes-bytes));
    calls=texture_upload_copy_calls;subresources=texture_upload_subresources;bytes=texture_upload_bytes;
}
static void image_upload_regions(image_entry *e,const uint8_t *storage,VkCommandBuffer upload_command) {
    const tex_t *t=&e->texture;
    uint64_t count=(uint64_t)t->L.layers*t->d.levels;
    if(!count)die("Vulkan empty texture upload");
    for(uint64_t first=0;first<count;){
        VkBufferImageCopy copies[TEXTURE_UPLOAD_REGIONS];uint64_t total=0;unsigned n=0;
        while(first+n<count&&n<(texture_upload_batch?TEXTURE_UPLOAD_REGIONS:1)){
            uint64_t i=first+n;unsigned level=i%t->d.levels,layer=i/t->d.levels;
            int bw,bh;uint32_t bpb=nvn_fmt_bpb(t->d.format,&bw,&bh);
            uint32_t w=dimension(t->d.width,level),h=dimension(t->d.height,level),d=t->d.target==NVN_TEXTURE_TARGET_3D?dimension(t->d.depth,level):1;
            if(!bpb||bw<=0||bh<=0)die("Vulkan upload block layout");
            uint64_t pitch=((uint64_t)w+bw-1)/bw*bpb,rows=((uint64_t)h+bh-1)/bh;
            if(pitch>UINT32_MAX||rows>UINT64_MAX/pitch||d>UINT64_MAX/(pitch*rows))die("Vulkan upload size overflow");
            uint64_t bytes=pitch*rows*d,alignment=bpb;
            // Vulkan buffer offsets must satisfy both four-byte and texel/block alignment.
            while(alignment%4)alignment+=bpb;
            uint64_t offset=(total+alignment-1)/alignment*alignment;
            if(n&&offset+bytes>TEXTURE_UPLOAD_BYTES)break;
            if(!bytes||bytes>(1ull<<32)||offset>(1ull<<32)-bytes)die("Vulkan upload buffer size");
            copies[n++]=(VkBufferImageCopy){.bufferOffset=offset,.imageSubresource={e->aspect,level,layer,1},.imageExtent={w,h,d}};
            total=offset+bytes;
        }
        post_op *p=post_new(3);p->source=buffer_create(total);
        for(unsigned i=0;i<n;i++){
            VkBufferImageCopy *r=&copies[i];unsigned level=r->imageSubresource.mipLevel,layer=r->imageSubresource.baseArrayLayer;
            int bw,bh;uint32_t bpb=nvn_fmt_bpb(t->d.format,&bw,&bh);
            uint64_t pitch=((uint64_t)r->imageExtent.width+bw-1)/bw*bpb,rows=((uint64_t)r->imageExtent.height+bh-1)/bh;
            uint8_t *dst=(uint8_t*)p->source.mapped+r->bufferOffset;
            if(t->d.flags&(NVN_TEXTURE_FLAGS_LINEAR|NVN_TEXTURE_FLAGS_LINEAR_RENDER_TARGET)){
                const uint8_t *src=storage+nvn_tex_level_offset(&t->d,level)+layer*t->L.layer_size;size_t stride=t->d.stride?t->d.stride:pitch;
                if(stride<pitch)die("Vulkan upload linear stride");
                for(unsigned z=0;z<r->imageExtent.depth;z++)for(uint64_t y=0;y<rows;y++)memcpy(dst+((uint64_t)z*rows+y)*pitch,src+((uint64_t)z*rows+y)*stride,pitch);
            }else if(nvn_deswizzle(&t->d,level,layer,storage,dst,pitch))die("Vulkan texture deswizzle failed");
            texture_upload_bytes+=pitch*rows*r->imageExtent.depth;
        }
        vkCmdCopyBufferToImage(upload_command,p->source.buffer,e->image,VK_IMAGE_LAYOUT_GENERAL,n,copies);
        texture_upload_copy_calls++;texture_upload_subresources+=n;first+=n;
    }
}
