// 图形管线与绘制；与描述符/计算绑定分开以便独立优化。
// Graphics pipelines and draws, separated from descriptor/compute binding for independent optimization.
static VkBlendFactor blend_factor(unsigned factor) {
    switch(factor){case 1:return VK_BLEND_FACTOR_ZERO;case 2:return VK_BLEND_FACTOR_ONE;case 3:return VK_BLEND_FACTOR_SRC_COLOR;case 4:return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;case 5:return VK_BLEND_FACTOR_SRC_ALPHA;case 6:return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;case 7:return VK_BLEND_FACTOR_DST_ALPHA;case 8:return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;case 9:return VK_BLEND_FACTOR_DST_COLOR;case 10:return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;case 11:return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;case 0x61:return VK_BLEND_FACTOR_CONSTANT_COLOR;case 0x62:return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;case 0x63:return VK_BLEND_FACTOR_CONSTANT_ALPHA;case 0x64:return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;default:return VK_BLEND_FACTOR_ONE;}
}
static VkBlendOp blend_operation(unsigned op){return op>=1&&op<=5?op-1:VK_BLEND_OP_ADD;}
static VkPrimitiveTopology topology(unsigned primitive){switch(primitive){case 0:return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;case 1:return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;case 3:return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;case 4:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;case 5:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;case 6:return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;default:die("Vulkan primitive %u pending",primitive);return 0;}}
static VkImageView attachment_view(image_entry *e,const view_t *v) {
    return image_view_get(e,VK_IMAGE_VIEW_TYPE_2D,texture_range(e,v));
}
#include "nvn_vulkan_pass.inc"

// 管线按真正的固定状态复用；视口、剪裁、颜色常量和模板掩码/参考值动态设置。
// Reuse pipelines by actual fixed state; viewport, scissor, blend constants and stencil masks/reference are dynamic.
typedef struct {
    shader_entry *vs,*fs; VkFormat formats[9]; uint32_t colors,primitive,attachments,depth_attachment,color_indices[8],required_subgroup_size;
    va_st attributes[16]; vs_st streams[16]; color_st color; chmask_st mask;
    blend_st blend[8]; poly_st polygon; ds_st depth; ms_st multisample;
} pipeline_key;
typedef struct pipeline_entry { pipeline_key key; VkPipeline pipeline; struct pipeline_entry *next; } pipeline_entry;
static pipeline_entry *graphics_pipelines[2048];
static uint64_t pipeline_creations, pipeline_reuses;
static pipeline_entry *graphics_last_pipeline;
static uint64_t pipeline_last_hits,pipeline_hash_calls;
static void graphics_pipeline_report(void) {
    static uint64_t frame,last_hits,hash_calls;
    if(!presents||presents%60||presents==frame)return;
    resource_create_report();
    uint64_t span=presents-frame,hits=pipeline_last_hits-last_hits,hashes=pipeline_hash_calls-hash_calls;
    LOG("[vulkan-pipeline-last] frame=%llu span=%llu key_bytes=%zu last_hits/frame=%.2f hash_calls/frame=%.2f hash_bytes_avoided/frame=%.0f full_key_compare=1",(unsigned long long)presents,(unsigned long long)span,sizeof(pipeline_key),hits/(double)span,hashes/(double)span,(double)hits*sizeof(pipeline_key)/span);
    frame=presents;last_hits=pipeline_last_hits;hash_calls=pipeline_hash_calls;
}
#include "nvn_vulkan_cmd_state.inc"
// 无片元着色器：全程序共用一份空描述符布局；运行期与预热共享同一对象，管线布局缓存键一致。
// No fragment shader: one empty descriptor-set layout for every program, one object shared by runtime and warm-up so layout cache keys agree.
static shader_entry *graphics_empty_shader(void) {
    static shader_entry empty={.stage=1,.set=1};
    if(!empty.layout){pthread_mutex_lock(&graphics_object_lock);
        if(!empty.layout){VkDescriptorSetLayoutCreateInfo empty_info={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};VK_CHECK(vkCreateDescriptorSetLayout(device,&empty_info,NULL,&empty.layout));}
        pthread_mutex_unlock(&graphics_object_lock);}
    return &empty;
}
// 逐字段从 key 派生图形管线创建状态，运行期与预热共用；动态值（视口/剪裁/混合常量/深度偏置/模板掩码与参考）
// 保持零或占位，真正取值由每次绘制的动态状态提交，创建结果只依赖固定状态，两处调用因此字节级一致。
// Derive graphics pipeline creation state from the key, shared by runtime and warm-up; dynamic values (viewport, scissor,
// blend constants, depth bias, stencil masks/reference) stay zero/placeholder and are applied per draw as dynamic state,
// so creation results depend on fixed state only and both callers agree byte for byte.
static VkResult graphics_pipeline_create(const pipeline_key *key,shader_entry *vs,shader_entry *fs,graphics_pass_entry *pass,VkPipelineLayout layout,VkPipeline *pipeline,int fatal) {
    if(key->colors>8){if(!fatal)return VK_ERROR_INITIALIZATION_FAILED;die("Vulkan color attachments %u pending",key->colors);}
    // key->primitive 已是拓扑转换后的 VkPrimitiveTopology；运行期只可能产出点/线/三角形六种，其余一律拒绝。
    // key->primitive is already the converted VkPrimitiveTopology; the runtime only ever produces the six point/line/triangle kinds, anything else is rejected.
    if(key->primitive>VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN){if(!fatal)return VK_ERROR_INITIALIZATION_FAILED;die("Vulkan primitive %u pending",key->primitive);}
    VkVertexInputAttributeDescription attrs[16];VkVertexInputBindingDescription streams[16];unsigned nattr=0,nstream=0;uint8_t stream_used[16]={0};
    for(unsigned i=0;i<16;i++){va_st a=key->attributes[i];if(!a.fmt||!vs->attributes[i])continue;VkFormat format=vk_format(a.fmt);
        if(format==VK_FORMAT_UNDEFINED){if(!fatal)return VK_ERROR_INITIALIZATION_FAILED;die("Vulkan vertex format %#x pending",a.fmt);}
        if(a.stream>15){if(!fatal)return VK_ERROR_INITIALIZATION_FAILED;die("Vulkan vertex stream %u pending",a.stream);}
        attrs[nattr++]=(VkVertexInputAttributeDescription){i,a.stream,format,a.offset};stream_used[a.stream]=1;}
    for(unsigned i=0;i<16;i++)if(stream_used[i]){const vs_st *s=&key->streams[i];
        if(s->divisor>1){if(!fatal)return VK_ERROR_INITIALIZATION_FAILED;die("Vulkan vertex divisor %u pending",s->divisor);}
        streams[nstream++]=(VkVertexInputBindingDescription){i,s->stride,s->divisor?VK_VERTEX_INPUT_RATE_INSTANCE:VK_VERTEX_INPUT_RATE_VERTEX};}
    VkPipelineVertexInputStateCreateInfo vertex={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,.vertexBindingDescriptionCount=nstream,.pVertexBindingDescriptions=streams,.vertexAttributeDescriptionCount=nattr,.pVertexAttributeDescriptions=attrs};
    VkPipelineInputAssemblyStateCreateInfo assembly={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=key->primitive};
    VkViewport viewport={0,0,1,1,0,1};VkRect2D scissor={{0,0},{1,1}};
    VkPipelineViewportStateCreateInfo view={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineRasterizationStateCreateInfo raster={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=key->polygon.cull,.frontFace=key->polygon.front?VK_FRONT_FACE_COUNTER_CLOCKWISE:VK_FRONT_FACE_CLOCKWISE,.depthBiasEnable=!!key->polygon.offset_en,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo multi={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT,.alphaToCoverageEnable=key->multisample.a2c};
    VkPipelineDepthStencilStateCreateInfo ds={.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,.depthTestEnable=key->depth.dt,.depthWriteEnable=key->depth.dw,.depthCompareOp=key->depth.df?key->depth.df-1:VK_COMPARE_OP_ALWAYS,.stencilTestEnable=key->depth.st};
    ds.front.failOp=key->depth.fail_f?key->depth.fail_f-1:VK_STENCIL_OP_KEEP;ds.front.passOp=key->depth.zpass_f?key->depth.zpass_f-1:VK_STENCIL_OP_KEEP;ds.front.depthFailOp=key->depth.zfail_f?key->depth.zfail_f-1:VK_STENCIL_OP_KEEP;ds.front.compareOp=key->depth.sf_f?key->depth.sf_f-1:VK_COMPARE_OP_ALWAYS;
    ds.back.failOp=key->depth.fail_b?key->depth.fail_b-1:VK_STENCIL_OP_KEEP;ds.back.passOp=key->depth.zpass_b?key->depth.zpass_b-1:VK_STENCIL_OP_KEEP;ds.back.depthFailOp=key->depth.zfail_b?key->depth.zfail_b-1:VK_STENCIL_OP_KEEP;ds.back.compareOp=key->depth.sf_b?key->depth.sf_b-1:VK_COMPARE_OP_ALWAYS;
    VkPipelineColorBlendAttachmentState blend[8];for(unsigned i=0;i<key->colors;i++){blend_st b=key->blend[i];blend[i]=(VkPipelineColorBlendAttachmentState){.blendEnable=(key->color.blend_en>>i)&1,.srcColorBlendFactor=blend_factor(b.src_rgb),.dstColorBlendFactor=blend_factor(b.dst_rgb),.colorBlendOp=blend_operation(b.eq_rgb),.srcAlphaBlendFactor=blend_factor(b.src_a),.dstAlphaBlendFactor=blend_factor(b.dst_a),.alphaBlendOp=blend_operation(b.eq_a),.colorWriteMask=(key->mask.mask>>(4*i))&15};}
    VkPipelineColorBlendStateCreateInfo color={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=key->colors,.pAttachments=blend};
    VkPipelineShaderStageCreateInfo stages[2]={{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=vs->module,.pName="main"},{.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=fs->module,.pName="main"}};
    VkDynamicState dynamic_states[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR,VK_DYNAMIC_STATE_BLEND_CONSTANTS,VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,VK_DYNAMIC_STATE_STENCIL_REFERENCE,VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dynamic={.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,.dynamicStateCount=sizeof dynamic_states/sizeof *dynamic_states,.pDynamicStates=dynamic_states};
#ifdef __ANDROID__
    VkPipelineShaderStageRequiredSubgroupSizeCreateInfo required[2];
    for(unsigned i=0;i<(fs->module?2u:1u);i++)
        if(!dm_vk_driver_prepare_shader_stage(&vk_api,&stages[i],&required[i])){if(!fatal)return VK_ERROR_INITIALIZATION_FAILED;die("Vulkan graphics subgroup policy: %s",dm_vk_driver_error(&vk_api));}
#endif
    VkGraphicsPipelineCreateInfo pi={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=fs->module?2:1,.pStages=stages,.pVertexInputState=&vertex,.pInputAssemblyState=&assembly,.pViewportState=&view,.pRasterizationState=&raster,.pMultisampleState=&multi,.pDepthStencilState=&ds,.pColorBlendState=&color,.pDynamicState=&dynamic,.layout=layout,.renderPass=pass->pass};
    return pipeline_create_graphics(&pi,pipeline);
}
// 真实创建才记录（缓存命中不写）；清单在 nvn_vulkan_warm.inc。
// Record genuine creations only (cache hits write nothing); the manifest lives in nvn_vulkan_warm.inc.
static void pso_record_graphics(const shader_entry *vs,const shader_entry *fs,const graphics_pass_key *pass,const pipeline_key *key);
static void draw_graphics_inner(const p_draw *draw,const p_draw_idx *indexed);
static void draw_graphics(const p_draw *draw,const p_draw_idx *indexed) {
    uint64_t phase=dm_cpu_phase_begin();draw_graphics_inner(draw,indexed);dm_cpu_phase_end(DM_CPU_PHASE_DRAW,phase);
}
static void draw_graphics_inner(const p_draw *draw,const p_draw_idx *indexed) {
    // 零顶点/零实例的 NVN 绘制不访问索引和资源，也不增加遮挡样本。
    // NVN draws with zero vertices/instances access no indices/resources and add no occlusion samples.
    if ((draw && draw->count <= 0) || (indexed && (indexed->count <= 0 || !indexed->instances))) return;
    snapshot_prepare();
    if(!state.program||(!state.rt.n&&!state.rt.depth))die("Vulkan draw without program/targets");shader_entry *vs=NULL,*fs=NULL;
    for(unsigned i=0;i<state.program->nstage;i++){shader_entry *s=program_shader(state.program,i);if(s->stage==0)vs=s;else if(s->stage==1)fs=s;else die("Vulkan graphics stage %u pending",s->stage);}
    if(!vs)die("Vulkan draw missing vertex stage");
    {uint64_t phase=dm_cpu_phase_begin();dependencies_prepare_draw(vs,fs,draw,indexed);dm_cpu_phase_end(DM_CPU_PHASE_DEPENDENCY,phase);}
    // 只在已有通道自然结束后提前提交；绑定和资源版本获取尚未开始。
    // Submit early only after the previous pass naturally ended, before binding or acquiring resource versions.
    if(!graphics_active_frame)async_chunk_boundary_locked();
    if(!fs)fs=graphics_empty_shader();
    post_op *const *output_scope=post_copy_tail;forward_scope_marker=output_scope;
    // 初始化列表的求值顺序在 C 里未定义；第二个集合的动态偏移接在第一个之后，必须按序调用。
    // Initializer-list evaluation order is unspecified in C; the second set's dynamic offsets follow the first,
    // so the two binds must be ordered statements.
    uint32_t dynamic_offsets[256];unsigned dynamic_count=0;
    uint64_t bind_phase=dm_cpu_phase_begin();
    VkDescriptorSet sets[2];
    sets[0]=bind_shader(vs,NULL,dynamic_offsets,&dynamic_count);
    sets[1]=bind_shader(fs,NULL,dynamic_offsets,&dynamic_count);
    dm_cpu_phase_end(DM_CPU_PHASE_BIND,bind_phase);
    uint64_t layout_phase=dm_cpu_phase_begin();VkPipelineLayout layout=graphics_layout_get(vs,fs);dm_cpu_phase_end(DM_CPU_PHASE_LAYOUT,layout_phase);
    VkImageView views[9]={0};image_entry *images[9]={0};VkAttachmentDescription attachments[9]={0};VkAttachmentReference colors[8]={0},depth={VK_ATTACHMENT_UNUSED,VK_IMAGE_LAYOUT_GENERAL};unsigned na=0;uint32_t width=UINT32_MAX,height=UINT32_MAX;
    for(int i=0;i<state.rt.n;i++){
        colors[i]=(VkAttachmentReference){VK_ATTACHMENT_UNUSED,VK_IMAGE_LAYOUT_GENERAL};if(!state.rt.color[i])continue;image_entry *e=image_get(state.rt.color[i]);const view_t *v=state.rt.has_cview[i]?&state.rt.cview[i]:NULL;VkImageSubresourceRange range=texture_range(e,v);uint32_t w=dimension(e->texture.d.width,range.baseMipLevel),h=dimension(e->texture.d.height,range.baseMipLevel);if(w<width)width=w;if(h<height)height=h;
        images[na]=e;views[na]=attachment_view(e,v);attachments[na]=(VkAttachmentDescription){.format=e->format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=e->populated?VK_ATTACHMENT_LOAD_OP_LOAD:VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,.initialLayout=VK_IMAGE_LAYOUT_GENERAL,.finalLayout=VK_IMAGE_LAYOUT_GENERAL};colors[i].attachment=na++;image_gpu_written(e);
    }
    if(state.rt.depth){image_entry *e=image_get(state.rt.depth);const view_t *v=state.rt.has_dview?&state.rt.dview:NULL;VkImageSubresourceRange range=texture_range(e,v);uint32_t w=dimension(e->texture.d.width,range.baseMipLevel),h=dimension(e->texture.d.height,range.baseMipLevel);if(w<width)width=w;if(h<height)height=h;images[na]=e;views[na]=attachment_view(e,v);attachments[na]=(VkAttachmentDescription){.format=e->format,.samples=VK_SAMPLE_COUNT_1_BIT,.loadOp=e->populated?VK_ATTACHMENT_LOAD_OP_LOAD:VK_ATTACHMENT_LOAD_OP_DONT_CARE,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,.stencilLoadOp=e->populated?VK_ATTACHMENT_LOAD_OP_LOAD:VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_STORE,.initialLayout=VK_IMAGE_LAYOUT_GENERAL,.finalLayout=VK_IMAGE_LAYOUT_GENERAL};depth.attachment=na++;image_gpu_written(e);}
    if(!na)die("Vulkan draw has no attachments");
    graphics_pass_key pass_key=graphics_pass_key_make(na,state.rt.n,attachments,colors,depth);graphics_frame_entry *frame;
    if(graphics_pass_matches(&pass_key,views,width,height))frame=graphics_active_frame;
    else {
        graphics_pass_end();
        for(unsigned i=0;i<na;i++)if(i==depth.attachment)image_access(images[i],VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
            else image_access(images[i],VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
        graphics_pass_entry *pass=graphics_pass_get(pass_key,attachments,colors,depth,1);frame=graphics_frame_get(pass,views,images,width,height);
    }
    // 只复制索引真正可访问的顶点前缀，避免每次绘制读取整段大型绑定池。
    // Copy only the vertex prefix reachable by the actual indices, rather than the entire large bound pool for every draw.
    VkBuffer index_buffer=VK_NULL_HANDLE;VkIndexType index_type=VK_INDEX_TYPE_UINT16;
    int64_t highest=draw?(int64_t)draw->first+draw->count-1:0;
    if(indexed){unsigned size=indexed->itype==0?1:indexed->itype==1?2:4;uint64_t bytes=(uint64_t)indexed->count*size;
        if(bytes>UINTPTR_MAX-indexed->ibuf)die("Vulkan draw index range overflow");
        // 这里在可能发生的 vertex dependency flush 后重新取得快照，不保留 preflight 的 handle。
        // Reacquire after any vertex-dependency flush rather than retaining the preflight handle.
        vk_buffer raw=snapshot_buffer(indexed->ibuf,bytes),indices=raw;uint32_t maximum=index_buffer_maximum(raw,size);
        if(size==1){post_op *p=post_new(3);p->source=buffer_create((uint64_t)indexed->count*2);for(int i=0;i<indexed->count;i++)((uint16_t*)p->source.mapped)[i]=((const uint8_t*)raw.mapped)[i];indices=p->source;}
        highest=(int64_t)maximum+indexed->base_vertex;index_buffer=indices.buffer;index_type=size==4?VK_INDEX_TYPE_UINT32:VK_INDEX_TYPE_UINT16;
    }
    if(highest<0)die("Vulkan negative vertex fetch");
    VkVertexInputAttributeDescription attrs[16];VkVertexInputBindingDescription streams[16];unsigned nattr=0,nstream=0;uint8_t stream_used[16]={0};uint32_t attribute_end[16]={0};VkBuffer vertex_buffers[16]={0};VkDeviceSize offsets[16]={0};
    for(unsigned i=0;i<16;i++){va_st a=state.attributes[i];if(!a.fmt||!vs->attributes[i])continue;VkFormat format=vk_format(a.fmt);if(format==VK_FORMAT_UNDEFINED)die("Vulkan vertex format %#x pending",a.fmt);attrs[nattr++]=(VkVertexInputAttributeDescription){i,a.stream,format,a.offset};stream_used[a.stream]=1;int bw,bh;uint32_t end=a.offset+nvn_fmt_bpb(a.fmt,&bw,&bh);if(end>attribute_end[a.stream])attribute_end[a.stream]=end;}
    for(unsigned i=0;i<16;i++)if(stream_used[i]){const vs_st *s=&state.streams[i];const NVNbufferRange *v=&state.vertices[i];if(s->divisor>1)die("Vulkan vertex divisor %u pending",s->divisor);if(!v->address||!v->size)die("Vulkan missing vertex buffer %u",i);streams[nstream++]=(VkVertexInputBindingDescription){i,s->stride,s->divisor?VK_VERTEX_INPUT_RATE_INSTANCE:VK_VERTEX_INPUT_RATE_VERTEX};uint64_t element=s->divisor?(indexed?(uint64_t)indexed->base_instance+indexed->instances-1:0):(uint64_t)highest;uint64_t needed=element*s->stride+attribute_end[i];if(needed>v->size)needed=v->size;VkDescriptorBufferInfo fetched=forward_readonly_buffer(v->address,needed,1,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT);vertex_buffers[i]=fetched.buffer;offsets[i]=fetched.offset;}
    VkPrimitiveTopology primitive=topology(draw?draw->prim:indexed->prim);
    // NVN 范围可反向或为常值，Vulkan 同样允许；默认 0..1 由状态初始化提供。
    // NVN permits reversed or constant ranges, as does Vulkan; state initialization supplies the 0..1 default.
    float *v=state.viewports.v[0];VkViewport viewport={v[0],v[1],v[2]>0?v[2]:width,v[3]>0?v[3]:height,state.depth_range.n,state.depth_range.f};
    // 限量记录实际范围供真机定位；最多 12 种，不执行读回或改变渲染。
    // Bound real-range diagnostics to 12 distinct pairs, without readbacks or rendering changes.
    static uint64_t depth_ranges_seen[12];static unsigned depth_ranges_count;uint64_t depth_range_bits;
    if(depth_ranges_count<12){memcpy(&depth_range_bits,&state.depth_range,sizeof depth_range_bits);unsigned i=0;while(i<depth_ranges_count&&depth_ranges_seen[i]!=depth_range_bits)i++;
        if(i==depth_ranges_count){depth_ranges_seen[depth_ranges_count++]=depth_range_bits;LOG("[vulkan-depth] range=%g,%g test=%u write=%u compare=%u target=%s",viewport.minDepth,viewport.maxDepth,(unsigned)state.depth.dt,(unsigned)state.depth.dw,(unsigned)state.depth.df,state.rt.depth?"depth":"color-only");}}
    int x=state.scissor.x>0?state.scissor.x:0,y=state.scissor.y>0?state.scissor.y:0;int sw=state.scissor.w>0?state.scissor.w:width,sh=state.scissor.h>0?state.scissor.h:height;if(x+sw>(int)width)sw=width-x;if(y+sh>(int)height)sh=height-y;if(sw<0||sh<0)die("Vulkan scissor outside target");VkRect2D scissor={{x,y},{sw,sh}};
    // Metal 的裁剪空间 Y 向上，Vulkan 的正高度视口 Y 向下；用负高度保持 NVN 画面方向。
    // Metal clip Y points up while Vulkan's positive viewport points down; a negative viewport preserves NVN orientation.
    viewport.y += viewport.height; viewport.height = -viewport.height;
    pipeline_key key={.vs=vs,.fs=fs,.colors=state.rt.n,.primitive=primitive,.color=state.color,.mask=state.mask,.polygon=state.polygon,.depth=depth.attachment!=VK_ATTACHMENT_UNUSED?state.depth:(ds_st){0},.multisample=state.multisample,.attachments=na,.depth_attachment=depth.attachment};
#ifdef __ANDROID__
    key.required_subgroup_size=vk_api.requested_subgroup_size;
#endif
    for(int i=0;i<state.rt.n;i++){key.color_indices[i]=colors[i].attachment;if(colors[i].attachment!=VK_ATTACHMENT_UNUSED)key.formats[i]=attachments[colors[i].attachment].format;if((state.color.blend_en>>i)&1)key.blend[i]=state.blend[i];}
    if(depth.attachment!=VK_ATTACHMENT_UNUSED)key.formats[8]=attachments[depth.attachment].format;
    for(unsigned i=0;i<16;i++){if(state.attributes[i].fmt&&vs->attributes[i])key.attributes[i]=state.attributes[i];if(stream_used[i])key.streams[i]=state.streams[i];}
    // 持久 pipeline 链只追加，queue_lock 串行访问；完整 key 相同才绕过哈希查找。
    // Persistent pipelines are append-only and accessed under queue_lock; only a full-key match bypasses hashing.
    unsigned pipeline_bucket=0;pipeline_entry *cached=graphics_last_pipeline;
    if(cached&&!memcmp(&cached->key,&key,sizeof key))pipeline_last_hits++;
    else{pipeline_hash_calls++;pipeline_bucket=graphics_hash(&key,sizeof key)&2047;cached=graphics_pipelines[pipeline_bucket];while(cached&&memcmp(&cached->key,&key,sizeof key))cached=cached->next;}
    if(!cached){resource_create_start creation=resource_create_begin();cached=calloc(1,sizeof *cached);if(!cached)die("Vulkan pipeline cache allocation");cached->key=key;
        // 只有未命中才派生创建管线的状态；每 draw 的动态状态仍在下面独立提交。创建与预热共用 graphics_pipeline_create，驱动缓存键一致。
        // Derive pipeline-creation state only on a miss; per-draw dynamic state is applied separately below. Creation shares graphics_pipeline_create with warm-up so driver cache keys agree.
        VK_CHECK(graphics_pipeline_create(&key,vs,fs,frame->key.pass,layout,&cached->pipeline,1));
        cached->next=graphics_pipelines[pipeline_bucket];graphics_pipelines[pipeline_bucket]=cached;pipeline_creations++;resource_create_end(1,creation,vs->key,fs->module?fs->key:NULL);
        pso_record_graphics(vs,fs,&frame->key.pass->key,&key);}else pipeline_reuses++;
    graphics_last_pipeline=cached;
    graphics_pass_begin(frame);
    graphics_command_apply(cached->pipeline,layout,sets,dynamic_offsets,dynamic_count,&viewport,&scissor,state.blend_color.rgba,state.polygon_offset.units,state.polygon_offset.factor,state.stencil_faces);
    for(unsigned i=0;i<16;i++)if(vertex_buffers[i])graphics_command_vertices(i,vertex_buffers[i],offsets[i]);
    graphics_probe_draw(draw,indexed);
    if(indexed){graphics_command_index(index_buffer,0,index_type);vkCmdDrawIndexed(command,indexed->count,indexed->instances,0,indexed->base_vertex,indexed->base_instance);}else vkCmdDraw(command,draw->count,1,draw->first,0);
    gpu_snapshot_consumer_encoded();
    forward_record_outputs(output_scope,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    draw_count++;
    // 无 shader 输出时保留 tile 内附件；可写缓冲/图像必须先结束通道再保证主机可见性。
    // Keep attachment tiles for shaders without outputs; writable buffers/images end the pass before host visibility.
    if(graphics_shader_writes(vs)||graphics_shader_writes(fs)){
        graphics_pass_end();if(shader_host_writes(vs)||shader_host_writes(fs)){VkMemoryBarrier writes={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_VERTEX_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&writes,0,NULL,0,NULL);}
    }
    batch_draws++;
    if (draw_count <= 12 && getenv("DM_VK_TRACE")) LOG("[vulkan] draw=%llu %s/%s rt=%ux%u vp=%g,%g %gx%g sc=%d,%d %dx%d mask=%#x cull=%u front=%u samples=%llu", (unsigned long long)draw_count, vs->key, fs->key, width, height, viewport.x, viewport.y, viewport.width, viewport.height, x, y, sw, sh, state.mask.mask, (unsigned)state.polygon.cull, (unsigned)state.polygon.front, (unsigned long long)samples_passed);
}
#include "nvn_vulkan_warm.inc"
