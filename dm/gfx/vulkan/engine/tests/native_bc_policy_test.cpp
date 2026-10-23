// 调用真实生产策略、块间距复制和blit规划；不声称执行Vulkan/GPU。
// Exercise actual production policy, block-pitch copying and blit planning; this does not execute Vulkan/GPU work.
#include "../xbg_bc_policy.h"
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { if(!(expression)){printf("FAIL line %d: %s\n",__LINE__,#expression);return 1;} } while(0)
int main(){
    CHECK(!xbg_native_bc_option(false,0,nullptr));
    CHECK(xbg_native_bc_option(false,0,"1"));
    const char *invalid_options[]={"0","","2","-1","1x","01"};
    for(const char *v:invalid_options)CHECK(!xbg_native_bc_option(false,0,v));
    CHECK(xbg_native_bc_option(true,'1',"0"));
    const int invalid_markers[]={'0','2','\n',EOF};
    for(int marker:invalid_markers)CHECK(!xbg_native_bc_option(true,marker,"1"));

    CHECK(xbg_bc_native_format(0x40)==VK_FORMAT_UNDEFINED);
    CHECK(xbg_bc_native_format(0x51)==VK_FORMAT_UNDEFINED);
    CHECK(xbg_bc_native_format(0x41)==VK_FORMAT_BC1_RGB_UNORM_BLOCK);
    CHECK(xbg_bc_native_format(0x42)==VK_FORMAT_BC1_RGBA_UNORM_BLOCK);
    CHECK(xbg_bc_native_format(0x43)==VK_FORMAT_BC2_UNORM_BLOCK);
    CHECK(xbg_bc_native_format(0x45)==VK_FORMAT_BC1_RGB_SRGB_BLOCK);
    CHECK(xbg_bc_native_format(0x46)==VK_FORMAT_BC1_RGBA_SRGB_BLOCK);
    CHECK(xbg_bc_native_format(0x47)==VK_FORMAT_BC2_SRGB_BLOCK);
    CHECK(xbg_bc_native_format(0x44)==VK_FORMAT_BC3_UNORM_BLOCK);
    CHECK(xbg_bc_native_format(0x48)==VK_FORMAT_BC3_SRGB_BLOCK);
    CHECK(xbg_bc_native_format(0x49)==VK_FORMAT_BC4_UNORM_BLOCK);
    CHECK(xbg_bc_native_format(0x4a)==VK_FORMAT_BC4_SNORM_BLOCK);
    CHECK(xbg_bc_native_format(0x4b)==VK_FORMAT_BC5_UNORM_BLOCK);
    CHECK(xbg_bc_native_format(0x4c)==VK_FORMAT_BC5_SNORM_BLOCK);
    CHECK(xbg_bc_native_format(0x4d)==VK_FORMAT_BC7_UNORM_BLOCK);
    CHECK(xbg_bc_native_format(0x4e)==VK_FORMAT_BC7_SRGB_BLOCK);
    CHECK(xbg_bc_native_format(0x4f)==VK_FORMAT_BC6H_SFLOAT_BLOCK);
    CHECK(xbg_bc_native_format(0x50)==VK_FORMAT_BC6H_UFLOAT_BLOCK);
    const VkFormatFeatureFlags features=VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    for(uint32_t f=0x41;f<=0x50;f++){
        bool rgba=(f<=0x48 || f==0x4d || f==0x4e);
        CHECK(xbg_bc_astc_destination(f)==rgba);
        CHECK(xbg_native_bc_eligible(f,true,true,features,1,4,0)==!rgba);
    }
    CHECK(!xbg_native_bc_eligible(0x49,false,true,features,1,1,0));
    CHECK(!xbg_native_bc_eligible(0x49,true,false,features,1,1,0));
    const VkFormatFeatureFlags required_bits[]={VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT,VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT,VK_FORMAT_FEATURE_TRANSFER_SRC_BIT,VK_FORMAT_FEATURE_TRANSFER_DST_BIT};
    for(VkFormatFeatureFlags bit:required_bits)
        CHECK(!xbg_native_bc_eligible(0x49,true,true,features&~bit,1,1,0));
    CHECK(!xbg_native_bc_eligible(0x49,true,true,features,4,1,0));
    CHECK(!xbg_native_bc_eligible(0x49,true,true,features,1,2,0));
    CHECK(!xbg_native_bc_eligible(0x49,true,true,features,1,1,0x4));
    CHECK(!xbg_native_bc_eligible(0x49,true,true,features,1,1,0x10));
    VkImageFormatProperties limits{};limits.maxExtent={64,32,1};limits.maxMipLevels=7;limits.maxArrayLayers=12;limits.sampleCounts=VK_SAMPLE_COUNT_1_BIT;
    CHECK(xbg_native_bc_extent_fits(limits,64,32,7,12));
    CHECK(!xbg_native_bc_extent_fits(limits,65,32,7,12));
    CHECK(!xbg_native_bc_extent_fits(limits,64,33,7,12));
    CHECK(!xbg_native_bc_extent_fits(limits,64,32,8,12));
    CHECK(!xbg_native_bc_extent_fits(limits,64,32,7,13));
    limits.sampleCounts=VK_SAMPLE_COUNT_4_BIT;CHECK(!xbg_native_bc_extent_fits(limits,64,32,7,12));

    XbgBlitRegion src{8,8,3,3,1,1,0,0,1,4,4,1},dst{8,8,4,3,1,2,0,0,1,4,4,1};
    VkImageBlit blit{};VkFilter filter=VK_FILTER_LINEAR;
    CHECK(xbg_astc_bc_plan(VK_FORMAT_ASTC_4x4_UNORM_BLOCK,VK_FORMAT_R8G8B8A8_UNORM,src,dst,0,VK_FORMAT_FEATURE_BLIT_SRC_BIT,VK_FORMAT_FEATURE_BLIT_DST_BIT,&blit,&filter)==0);
    CHECK(filter==VK_FILTER_NEAREST && blit.srcSubresource.mipLevel==1 && blit.srcSubresource.baseArrayLayer==2 && blit.dstSubresource.baseArrayLayer==3);
    CHECK(blit.srcSubresource.layerCount==1 && blit.dstOffsets[1].x==4 && blit.dstOffsets[1].y==4);
    CHECK(xbg_astc_bc_plan(VK_FORMAT_ASTC_4x4_SRGB_BLOCK,VK_FORMAT_R8G8B8A8_SRGB,src,dst,1,VK_FORMAT_FEATURE_BLIT_SRC_BIT,VK_FORMAT_FEATURE_BLIT_DST_BIT,&blit,&filter)==0 && filter==VK_FILTER_NEAREST);
    CHECK(xbg_astc_bc_plan(VK_FORMAT_ASTC_4x4_SRGB_BLOCK,VK_FORMAT_R8G8B8A8_UNORM,src,dst,0,VK_FORMAT_FEATURE_BLIT_SRC_BIT,VK_FORMAT_FEATURE_BLIT_DST_BIT,&blit,&filter)!=0);
    CHECK(xbg_astc_bc_plan(VK_FORMAT_ASTC_5x4_UNORM_BLOCK,VK_FORMAT_R8G8B8A8_UNORM,src,dst,0,VK_FORMAT_FEATURE_BLIT_SRC_BIT,VK_FORMAT_FEATURE_BLIT_DST_BIT,&blit,&filter)!=0);
    CHECK(xbg_astc_bc_plan(VK_FORMAT_ASTC_4x4_UNORM_BLOCK,VK_FORMAT_R8G8B8A8_UNORM,src,dst,4,VK_FORMAT_FEATURE_BLIT_SRC_BIT,VK_FORMAT_FEATURE_BLIT_DST_BIT,&blit,&filter)!=0);
    CHECK(xbg_astc_bc_plan(VK_FORMAT_ASTC_4x4_UNORM_BLOCK,VK_FORMAT_R8G8B8A8_UNORM,src,dst,0,0,VK_FORMAT_FEATURE_BLIT_DST_BIT,&blit,&filter)!=0);
    CHECK(xbg_astc_bc_plan(VK_FORMAT_ASTC_4x4_UNORM_BLOCK,VK_FORMAT_R8G8B8A8_UNORM,src,dst,0,VK_FORMAT_FEATURE_BLIT_SRC_BIT,0,&blit,&filter)!=0);
    dst.z=2;CHECK(xbg_astc_bc_plan(VK_FORMAT_ASTC_4x4_UNORM_BLOCK,VK_FORMAT_R8G8B8A8_UNORM,src,dst,0,VK_FORMAT_FEATURE_BLIT_SRC_BIT,VK_FORMAT_FEATURE_BLIT_DST_BIT,&blit,&filter)!=0);dst.z=1;
    CHECK(!xbg_copy_is_full(dst));
    CHECK(!xbg_copy_is_full({8,8,4,1,0,0,0,0,0,8,8,1}));
    CHECK(xbg_copy_is_full({8,8,4,1,0,0,0,0,0,8,8,4}));

    XbgBcCopyRows p{};CHECK(xbg_bc_copy_rows(5,7,2,16,48,160,&p)==0);
    CHECK(p.tight==32 && p.rows==2 && p.row==48 && p.image==160 && p.guest_bytes==320 && p.packed_bytes==128);
    unsigned char guest[320],packed[128],restored[320];
    for(size_t i=0;i<sizeof guest;i++)guest[i]=static_cast<unsigned char>(i*17+13);
    memset(restored,0xcd,sizeof restored);
    CHECK(xbg_bc_copy_transfer(packed,sizeof packed,guest,sizeof guest,p,false)==0);
    CHECK(xbg_bc_copy_transfer(restored,sizeof restored,packed,sizeof packed,p,true)==0);
    for(size_t i=0;i<sizeof restored;i++){
        bool block_byte=(i%160)<32 || ((i%160)>=48 && (i%160)<80);
        CHECK(restored[i]==(block_byte?guest[i]:0xcd));
    }
    CHECK(xbg_bc_copy_transfer(restored,sizeof restored-1,packed,sizeof packed,p,true)!=0);
    CHECK(xbg_bc_copy_transfer(packed,sizeof packed,guest,sizeof guest-1,p,false)!=0);
    CHECK(xbg_bc_copy_rows(5,7,2,16,31,160,&p)!=0);
    CHECK(xbg_bc_copy_rows(5,7,2,16,48,95,&p)!=0);
    CHECK(xbg_bc_copy_rows(5,7,2,16,-1,0,&p)!=0);
    CHECK(xbg_bc_copy_rows(5,7,0,16,0,0,&p)!=0);
    CHECK(xbg_bc_copy_rows(0,7,2,16,0,0,&p)!=0);
    CHECK(xbg_bc_copy_rows(5,7,2,16,INT64_MAX,0,&p)!=0);
    CHECK(xbg_bc_copy_rows(1,1,1,8,0,0,&p)==0 && p.packed_bytes==8);
    CHECK(xbg_bc_copy_rows(1,1,1,16,0,0,&p)==0 && p.packed_bytes==16);
    puts("PASS production native-BC policy, ASTC blit planner, mip/layer preservation decisions and block copy pitches; GPU NOT RUN");
    return 0;
}
