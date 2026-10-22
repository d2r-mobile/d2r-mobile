# Production Android shader boundary / 生产着色器边界

This describes `dm/android/native/nvn_vulkan_shader.inc`, not the older standalone Vulkan test backend. `dm/android/tools/build_shaders.py` translates the prepared shader input and writes SPIR-V plus compact `DMVK` reflection. Shader instruction semantics belong to that translator; the reflection check below is not a complete SPIR-V validator.

本文针对实际可玩后端 `nvn_vulkan_shader.inc`，不把旧独立 Vulkan 测试模块视为生产后端。`build_shaders.py` 生成 SPIR-V 和 `DMVK` 反射；下列校验不能替代完整的 SPIR-V 指令语义验证。

| Supported stage / 支持阶段 | NVN stage | Reflection set / 反射集合 |
|---|---:|---:|
| Vertex / 顶点 | 0 | 0 |
| Fragment / 像素 | 1 | 1 |
| Compute / 计算 | 5 | 0 |

Geometry (2), tessellation control (3), tessellation evaluation (4), unknown stages and other stage/set pairs are rejected before module/layout creation or indexing stage arrays. Translator support for generating another stage does not imply runtime pipeline support. Required loads report `Invalid Vulkan reflection`; optional warm loads return no shader and release the metadata/lock.

几何阶段2、细分控制3、细分求值4、未知阶段和错误的阶段／集合组合均在创建模块／布局和索引阶段数组之前拒绝。工具能生成其他阶段不代表后端支持对应管线。必需加载明确报反射错误；可选预热跳过并释放反射内存和锁。

The version-1 binary header has seven 32-bit words: magic, version, stage, set, binding count, c1 byte count and attribute count. It is followed by 40 bytes per binding, c1 bytes and 32 attribute words. Exact file length is required. Current limits are 256 bindings, 64 KiB c1, binding kinds 0–4 and descriptor arrays of 1–128 elements. Binding numbers must be unique. These are consumer bounds, not claims that every Vulkan device accepts their combined resource usage; physical device limits and Vulkan creation results still apply.

版本1头部为7个32位字：标记、版本、阶段、集合、binding数、c1字节数和属性数；后接每binding 40字节、c1和32个属性字。文件长度必须准确，消费者上限为256个binding、64KiB c1、种类0–4、数组长度1–128，binding号不可重复。这些不代表所有设备都支持上限资源组合，实际设备限制和Vulkan创建结果仍需检查。

Capability gating in `nvn_vulkan_caps.h` separately handles tessellation, storage-image writes without format, multi-viewport and shader draw parameters. Pipeline disk cache validation and driver/device namespaces are exercised by `pipeline_disk_cache_host`; reflection bounds by `shader_reflection`. The tests use owned inputs. They do not establish every shader instruction, SPIR-V/reflection agreement or Adreno validation-layer result. Remaining acceptance stays in  and .

能力门控和磁盘管线缓存另有回归；反射边界由 `shader_reflection` 测试自有输入。完整指令语义、SPIR-V／反射一致性和Adreno验证层仍由 #12／#42 跟踪，未以宿主测试代替实机验收。
