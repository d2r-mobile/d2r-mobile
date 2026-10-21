// #246 minimal testable boundary for VkShaderModule creation. The caller owns
// the reflection/code/entry allocations and the already-held graphics lock.
// On optional failure this helper performs the exact production cleanup; on
// required failure it keeps the fatal path.
typedef VkResult (*dm_nvn_module_create_fn)(VkDevice, const VkShaderModuleCreateInfo *,
                                            const VkAllocationCallbacks *, VkShaderModule *);

static VkResult shader_module_create_or_skip(
    dm_nvn_module_create_fn create, VkDevice device, const VkShaderModuleCreateInfo *info,
    VkShaderModule *module, int fatal, const char *key, void *code, void *meta,
    void *entry, pthread_mutex_t *lock) {
    (void)key;
    VkResult result = create(device, info, NULL, module);
    if (result == VK_SUCCESS) return result;
    if (fatal)
        die("Vulkan shader module creation failed for %s: VkResult=%d", key, (int)result);
    LOG("[vulkan] optional shader %s module creation skipped: VkResult=%d", key, (int)result);
    free(code);
    free(meta);
    free(entry);
    pthread_mutex_unlock(lock);
    return result;
}
