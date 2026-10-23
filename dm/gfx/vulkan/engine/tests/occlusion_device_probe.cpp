// 独立真机正确性小样：无 Surface/游戏资源/文件操作，不测游戏性能。
// Standalone device correctness probe: no Surface, game assets, files or FPS claims.
// Build separately with Vulkan headers/loader; never link into the game library.
#include <vulkan/vulkan.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "../xbg_clear_spv.h"

namespace {
constexpr uint32_t count=11,side=32;
void check(VkResult result,const char *call){if(result!=VK_SUCCESS){
    std::fprintf(stderr,"FAIL %s VkResult=%d\n",call,int(result));throw std::runtime_error(call);}}
#define VK(call) check((call),#call)
struct Probe {
    VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};
    uint32_t family=0;VkPhysicalDeviceMemoryProperties memories{};
    VkImage image{};VkDeviceMemory image_memory{};VkImageView view{};
    VkRenderPass pass{};VkFramebuffer framebuffer{};VkShaderModule vs{},fs{};
    VkPipelineLayout layout{};VkPipeline pipeline{};VkCommandPool commands{};
    VkCommandBuffer cmd{};VkQueryPool queries{};VkBuffer buffer{};VkDeviceMemory memory{};
    VkFence fence{};void *mapped=nullptr;bool coherent=false;
    ~Probe(){
        if(device){vkDeviceWaitIdle(device);if(mapped)vkUnmapMemory(device,memory);
            if(fence)vkDestroyFence(device,fence,nullptr);
            if(commands)vkDestroyCommandPool(device,commands,nullptr);
            if(queries)vkDestroyQueryPool(device,queries,nullptr);
            if(buffer)vkDestroyBuffer(device,buffer,nullptr);if(memory)vkFreeMemory(device,memory,nullptr);
            if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
            if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
            if(vs)vkDestroyShaderModule(device,vs,nullptr);if(fs)vkDestroyShaderModule(device,fs,nullptr);
            if(framebuffer)vkDestroyFramebuffer(device,framebuffer,nullptr);
            if(pass)vkDestroyRenderPass(device,pass,nullptr);if(view)vkDestroyImageView(device,view,nullptr);
            if(image)vkDestroyImage(device,image,nullptr);if(image_memory)vkFreeMemory(device,image_memory,nullptr);
            vkDestroyDevice(device,nullptr);}
        if(instance)vkDestroyInstance(instance,nullptr);
    }
    uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags required,VkMemoryPropertyFlags preferred=0){
        for(unsigned preference=0;preference<2;++preference)for(uint32_t i=0;i<memories.memoryTypeCount;++i)
            if((bits&(1u<<i)) && (memories.memoryTypes[i].propertyFlags&required)==required &&
               (preference || (memories.memoryTypes[i].propertyFlags&preferred)==preferred))return i;
        throw std::runtime_error("No compatible memory type");
    }
    void initialize(){
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="occlusion-device-probe";app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo ic{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ic.pApplicationInfo=&app;VK(vkCreateInstance(&ic,nullptr,&instance));
        uint32_t n=0;VK(vkEnumeratePhysicalDevices(instance,&n,nullptr));std::vector<VkPhysicalDevice> devices(n);
        VK(vkEnumeratePhysicalDevices(instance,&n,devices.data()));
        for(auto candidate:devices){VkPhysicalDeviceProperties p;vkGetPhysicalDeviceProperties(candidate,&p);
            VkPhysicalDeviceFeatures f;vkGetPhysicalDeviceFeatures(candidate,&f);
            if(p.apiVersion<VK_API_VERSION_1_1 || !f.occlusionQueryPrecise)continue;
            uint32_t qn=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&qn,nullptr);std::vector<VkQueueFamilyProperties> qs(qn);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate,&qn,qs.data());
            for(uint32_t q=0;q<qn;++q)if(qs[q].queueCount && (qs[q].queueFlags&VK_QUEUE_GRAPHICS_BIT)){physical=candidate;family=q;break;}
            if(physical)break;
        }
        if(!physical)throw std::runtime_error("Vulkan1.1 graphics device with PRECISE queries required");
        VkPhysicalDeviceProperties p;vkGetPhysicalDeviceProperties(physical,&p);
        std::printf("GPU=%s API=%u.%u.%u driver=%u vendor=%#x device=%#x\n",p.deviceName,
                    VK_VERSION_MAJOR(p.apiVersion),VK_VERSION_MINOR(p.apiVersion),VK_VERSION_PATCH(p.apiVersion),p.driverVersion,p.vendorID,p.deviceID);
        float priority=1;VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qc.queueFamilyIndex=family;qc.queueCount=1;qc.pQueuePriorities=&priority;
        VkPhysicalDeviceFeatures features{};features.occlusionQueryPrecise=VK_TRUE;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qc;dc.pEnabledFeatures=&features;
        VK(vkCreateDevice(physical,&dc,nullptr,&device));vkGetDeviceQueue(device,family,0,&queue);vkGetPhysicalDeviceMemoryProperties(physical,&memories);
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ii.imageType=VK_IMAGE_TYPE_2D;ii.format=VK_FORMAT_R8G8B8A8_UNORM;ii.extent={side,side,1};
        ii.mipLevels=ii.arrayLayers=1;ii.samples=VK_SAMPLE_COUNT_1_BIT;ii.tiling=VK_IMAGE_TILING_OPTIMAL;ii.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;ii.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        VK(vkCreateImage(device,&ii,nullptr,&image));VkMemoryRequirements req;vkGetImageMemoryRequirements(device,image,&req);
        VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=req.size;ma.memoryTypeIndex=memory_type(req.memoryTypeBits,0,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK(vkAllocateMemory(device,&ma,nullptr,&image_memory));VK(vkBindImageMemory(device,image,image_memory,0));
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=image;vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=ii.format;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        VK(vkCreateImageView(device,&vi,nullptr,&view));
        VkAttachmentDescription a{};a.format=ii.format;a.samples=ii.samples;a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;a.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;a.initialLayout=a.finalLayout=VK_IMAGE_LAYOUT_GENERAL;
        VkAttachmentReference color{0,VK_IMAGE_LAYOUT_GENERAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&color;
        VkSubpassDependency dep[2]={{VK_SUBPASS_EXTERNAL,0,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,0},
            {0,VK_SUBPASS_EXTERNAL,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
             VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,0}};
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rp.attachmentCount=1;rp.pAttachments=&a;rp.subpassCount=1;rp.pSubpasses=&sub;rp.dependencyCount=2;rp.pDependencies=dep;
        VK(vkCreateRenderPass(device,&rp,nullptr,&pass));VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fb.renderPass=pass;fb.attachmentCount=1;fb.pAttachments=&view;fb.width=fb.height=side;fb.layers=1;
        VK(vkCreateFramebuffer(device,&fb,nullptr,&framebuffer));
        VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};sm.codeSize=sizeof(xbg_clear_vs);sm.pCode=xbg_clear_vs;VK(vkCreateShaderModule(device,&sm,nullptr,&vs));
        sm.codeSize=sizeof(xbg_clear_fs_float);sm.pCode=xbg_clear_fs_float;VK(vkCreateShaderModule(device,&sm,nullptr,&fs));
        VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT,0,16};VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push;
        VK(vkCreatePipelineLayout(device,&pl,nullptr,&layout));
        VkPipelineShaderStageCreateInfo stages[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_VERTEX_BIT,vs,"main",nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_FRAGMENT_BIT,fs,"main",nullptr}};
        VkPipelineVertexInputStateCreateInfo vertex{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};vp.viewportCount=vp.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};ms.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        VkPipelineColorBlendAttachmentState ba{};ba.colorWriteMask=15;VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blend.attachmentCount=1;blend.pAttachments=&ba;
        VkDynamicState dyn[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dyn;
        VkGraphicsPipelineCreateInfo pc{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pc.stageCount=2;pc.pStages=stages;pc.pVertexInputState=&vertex;pc.pInputAssemblyState=&assembly;pc.pViewportState=&vp;
        pc.pRasterizationState=&raster;pc.pMultisampleState=&ms;pc.pDepthStencilState=&depth;pc.pColorBlendState=&blend;pc.pDynamicState=&dynamic;pc.layout=layout;pc.renderPass=pass;
        VK(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pc,nullptr,&pipeline));
        VkQueryPoolCreateInfo qp{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};qp.queryType=VK_QUERY_TYPE_OCCLUSION;qp.queryCount=count;VK(vkCreateQueryPool(device,&qp,nullptr,&queries));
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};bi.size=count*sizeof(uint64_t);bi.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;bi.sharingMode=VK_SHARING_MODE_EXCLUSIVE;VK(vkCreateBuffer(device,&bi,nullptr,&buffer));
        vkGetBufferMemoryRequirements(device,buffer,&req);ma.allocationSize=req.size;ma.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        coherent=(memories.memoryTypes[ma.memoryTypeIndex].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
        VK(vkAllocateMemory(device,&ma,nullptr,&memory));VK(vkBindBufferMemory(device,buffer,memory,0));VK(vkMapMemory(device,memory,0,VK_WHOLE_SIZE,0,&mapped));
        std::memset(mapped,0xff,count*sizeof(uint64_t));if(!coherent){VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=memory;range.size=VK_WHOLE_SIZE;VK(vkFlushMappedMemoryRanges(device,1,&range));}
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cp.queueFamilyIndex=family;VK(vkCreateCommandPool(device,&cp,nullptr,&commands));
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=commands;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;VK(vkAllocateCommandBuffers(device,&ca,&cmd));
        VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};VK(vkCreateFence(device,&fc,nullptr,&fence));
    }
    void begin_pass(){VkClearValue zero{};VkRenderPassBeginInfo b{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};b.renderPass=pass;b.framebuffer=framebuffer;b.renderArea.extent={side,side};b.clearValueCount=1;b.pClearValues=&zero;
        vkCmdBeginRenderPass(cmd,&b,VK_SUBPASS_CONTENTS_INLINE);vkCmdBindPipeline(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);
        VkViewport v{0,0,float(side),float(side),0,1};vkCmdSetViewport(cmd,0,1,&v);float rgba[4]={1,0,0,1};vkCmdPushConstants(cmd,layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,16,rgba);}
    void draw(uint32_t size){VkRect2D sc{{0,0},{size,size}};vkCmdSetScissor(cmd,0,1,&sc);vkCmdDraw(cmd,3,1,0,0);}
    void start(unsigned q){vkCmdBeginQuery(cmd,queries,q,VK_QUERY_CONTROL_PRECISE_BIT);}
    void end(unsigned q){vkCmdEndQuery(cmd,queries,q);}
    bool run(){
        VkCommandBufferBeginInfo cb{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};cb.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;VK(vkBeginCommandBuffer(cmd,&cb));
        vkCmdResetQueryPool(cmd,queries,0,count);
        VkImageMemoryBarrier im{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};im.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;im.newLayout=VK_IMAGE_LAYOUT_GENERAL;im.image=image;
        im.srcQueueFamilyIndex=im.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;im.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};im.dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,nullptr,0,nullptr,1,&im);
        begin_pass();
        // 同一组3 draw：1024+256+64=1344，逐draw与聚合应相等。
        // Three draws: 1024+256+64=1344, separate and grouped queries must agree.
        const uint32_t sizes[3]={32,16,8};for(unsigned i=0;i<3;++i){start(i);draw(sizes[i]);end(i);}
        start(3);for(auto size:sizes)draw(size);end(3);
        // clear前后分段；排除真正ClearAttachments及模拟masked-clear的合成draw。
        // Split around both ClearAttachments and a synthetic masked-clear draw.
        start(4);draw(32);end(4);
        VkClearAttachment clear{};clear.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;clear.colorAttachment=0;
        VkClearRect rect{{{0,0},{32,32}},0,1};vkCmdClearAttachments(cmd,1,&clear,1,&rect);draw(32);
        start(5);draw(16);end(5);
        // 两epoch，epoch0的report前缀1024、末尾1280；epoch1为64+256=320。
        // Two epochs: epoch0 report prefix 1024, final 1280; epoch1 total 320.
        start(6);draw(32);end(6); // report boundary, no query may straddle it
        start(7);draw(16);end(7); // reset boundary before starting epoch1
        start(8);draw(8);draw(16);end(8);
        // 同一逻辑counter跨两个renderpass，必须拆query：1280+64=1344。
        // One logical counter across two renderpasses: split queries, 1280+64=1344.
        start(9);draw(32);draw(16);end(9);vkCmdEndRenderPass(cmd);
        begin_pass();start(10);draw(8);end(10);vkCmdEndRenderPass(cmd);
        // 有意与生产相同：64_BIT且不加WAIT；哨兵能检出未写回的结果。
        // Deliberately match production: 64_BIT without WAIT; detect unwritten results.
        vkCmdCopyQueryPoolResults(cmd,queries,0,count,buffer,0,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT);
        VkMemoryBarrier host{VK_STRUCTURE_TYPE_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&host,0,nullptr,0,nullptr);
        VK(vkEndCommandBuffer(cmd));VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&cmd;VK(vkQueueSubmit(queue,1,&submit,fence));
        VK(vkWaitForFences(device,1,&fence,VK_TRUE,10000000000ull));
        if(!coherent){VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=memory;range.size=VK_WHOLE_SIZE;VK(vkInvalidateMappedMemoryRanges(device,1,&range));}
        uint64_t copied[count],direct[count]{};std::memcpy(copied,mapped,sizeof copied);
        VK(vkGetQueryPoolResults(device,queries,0,count,sizeof direct,direct,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT));
        const uint64_t expected[count]={1024,256,64,1344,1024,256,1024,256,320,1280,64};bool good=true;
        for(unsigned i=0;i<count;++i){std::printf("query=%u get=%llu copy=%llu expected=%llu\n",i,(unsigned long long)direct[i],(unsigned long long)copied[i],(unsigned long long)expected[i]);
            good&=direct[i]==expected[i] && copied[i]==direct[i];}
        std::printf("separate_sum=%llu batched=%llu clear_excluded=%llu epoch0_prefix=%llu epoch0_total=%llu epoch1_total=%llu two_pass_sum=%llu\n",
            (unsigned long long)(direct[0]+direct[1]+direct[2]),(unsigned long long)direct[3],(unsigned long long)(direct[4]+direct[5]),
            (unsigned long long)direct[6],(unsigned long long)(direct[6]+direct[7]),(unsigned long long)direct[8],(unsigned long long)(direct[9]+direct[10]));
        good&=direct[0]+direct[1]+direct[2]==direct[3] && direct[4]+direct[5]==1280 && direct[6]+direct[7]==1280 && direct[9]+direct[10]==1344;
        std::printf("%s queries=%u (device correctness only; not game FPS)\n",good?"PASS":"FAIL",count);return good;
    }
};
}
int main(){try{Probe probe;probe.initialize();return probe.run()?0:1;}
    catch(const std::exception &e){std::fprintf(stderr,"FAIL %s\n",e.what());return 2;}}
