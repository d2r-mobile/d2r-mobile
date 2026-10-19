// Ryujinx 的 OR 两输入可交换；读取实际 driver words 后合成，不能按输入位置猜贴图/采样器。
// Ryujinx's OR operands can be swapped; combine real driver words instead of inferring texture/sampler from operand order.
static unsigned shader_handle_bank(const shader_binding *binding,unsigned operand) {
    if(binding->cb_slot==UINT32_MAX)return 2;unsigned primary=binding->cb_slot&0xffff,secondary=binding->cb_slot>>16;
    return operand?(secondary?secondary-1:primary):primary;
}
static uint32_t shader_handle_word(unsigned stage,unsigned bank,unsigned offset) {
    if(stage>=6)die("Vulkan texture-handle stage %u",stage);
    // 当前全部 6,931 个游戏 shader 均从 c2 取句柄；其他常量银行不允许静默错绑。
    // All current 6,931 game shaders fetch handles from c2; other banks must not silently bind incorrect resources.
    if(bank!=2)die("Vulkan texture-handle bank %u is unsupported",bank);
    if(offset&7)return 0;
    if(offset>=0x168&&offset<0x568)return state.textures[stage][(offset-0x168)/8];
    if(offset>=0x568&&offset<0x968)return state.samplers[stage][(offset-0x568)/8];
    if(offset>=0x120&&offset<0x168)return state.images[stage][(offset-0x120)/8];
    return 0;
}
static uint32_t shader_handle(const shader_binding *binding,unsigned stage,unsigned element) {
    unsigned type=binding->handle>>28,offset0=(binding->handle&0x3fff)*4,offset1=((binding->handle>>14)&0x3fff)*4;
    uint32_t first=shader_handle_word(stage,shader_handle_bank(binding,0),offset0+8*element);
    switch(type){
        case 0:return first;
        case 1:return first|shader_handle_word(stage,shader_handle_bank(binding,1),offset1+8*element);
        case 2:return first|(shader_handle_word(stage,shader_handle_bank(binding,1),offset1+8*element)<<20);
        case 3:return first|(((binding->handle>>14)&0x3fff)<<20);
        default:die("Vulkan texture handle type %u is unsupported",type);return 0;
    }
}
static int shader_texture_slot(const shader_binding *binding,unsigned element) {
    unsigned offsets[2]={(binding->handle&0x3fff)*4,((binding->handle>>14)&0x3fff)*4},count=(binding->handle>>28)==1||((binding->handle>>28)==2)?2:1;
    for(unsigned i=0;i<count;i++){unsigned offset=offsets[i]+8*element;if(shader_handle_bank(binding,i)==2&&offset>=0x168&&offset<0x568&&!(offset&7))return (offset-0x168)/8;}
    return -1;
}
static int shader_image_slot(const shader_binding *binding,unsigned element) {
    unsigned offset=(binding->handle&0x3fff)*4+8*element;return shader_handle_bank(binding,0)==2&&offset>=0x120&&offset<0x168&&!(offset&7)?(int)((offset-0x120)/8):-1;
}
