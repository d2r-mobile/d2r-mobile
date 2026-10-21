static texture_descriptor descriptor_texture(int id) {
    texture_descriptor d={0};pthread_mutex_lock(&descriptor_lock);if(id>=0&&id<texture_capacity)d=texture_descriptors[id];pthread_mutex_unlock(&descriptor_lock);return d;
}
static VkSampler descriptor_sampled(uint32_t handle,uint32_t type,texture_descriptor *texture) {
    pthread_mutex_lock(&descriptor_lock);
    VkSampler sampler=sampler_get_for_type_locked((handle>>20)&0xfff,type);
    unsigned id=handle&0xfffff;
    if(texture)*texture=id<(unsigned)texture_capacity?texture_descriptors[id]:(texture_descriptor){0};
    pthread_mutex_unlock(&descriptor_lock);return sampler;
}
