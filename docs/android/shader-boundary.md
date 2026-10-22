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

Before creating a module, required and optional runtime loads check SPIR-V framing, the single `main` entry and its execution model against DMVK. A required mismatch reports `Invalid Vulkan shader entry/stage`; optional warm loads clean up and skip it.

创建模块之前，必需加载和可选预热都会检查 SPIR-V 指令边界、唯一 `main` 入口及其阶段与 DMVK 一致。必需加载不符时报入口／阶段错误，可选预热清理后跳过。

The shader builder calls `shader_pair_contract.write_metadata` before publishing each compact reflection. It checks descriptor set/binding, buffer/image/sampler kind, literal array count, image dimension/array/MS/depth flags and vertex input location/component type. The shared support UBO is allowed at set 0/binding 0 even for fragment shaders. Unused layout bindings may remain in DMVK, as Vulkan permits layout supersets. Texel buffers accept sampled-image wrappers because the consumer uses a texel-buffer descriptor. This gate also checks generated tessellation/geometry pairs; it does not add runtime support for those stages. Existing imported caches are not retrospectively certified by the build gate. Audit a local directory with `python3 dm/android/tools/shader_pair_contract.py /absolute/path/to/spirv`.

生成工具在发布每份紧凑反射之前强制核对集合／binding、缓冲／图像／采样器种类、常量数组长度、图像维度／数组／多采样／深度标志，以及顶点输入位置和分量类型。像素阶段可共用 set 0/binding 0 支持 UBO；反射可保留 SPIR-V 不再使用的布局项；texel-buffer 按消费者语义接受 sampled-image 包装。生成工具检查其他阶段的配对并不新增对应运行期支持。旧导入缓存不因新生成闸门而自动获得认证，可用上述命令只读检查本地目录。

Capability gating and pipeline disk-cache checks remain separate. `shader_reflection`, `shader_pair_contract` and `pipeline_disk_cache_host` use owned inputs. This is not complete SPIR-V validation: constant-buffer member offsets/access bounds, c1 content, inter-stage I/O linkage, image formats, translated instruction semantics and Adreno validation-layer results remain in  and . Retain `spirv-val` for instruction/type/control-flow validation.

能力门控、磁盘缓存和配对测试各有回归。常量缓冲成员布局／访问边界、c1 内容、阶段间输入输出连接、图像格式、翻译指令语义及 Adreno 验证层仍由 #12／#42 跟踪，宿主测试不代替实机验收；指令／类型／控制流检查仍需保留 `spirv-val`。
