# 同步辅助函数：参与者、保留状态与内存序证明材料（#4 / A2）

# Sync helpers: participants, reservation state and memory-order proof material (#4 / A2)

状态：**准备与自有反例，不是实现或验收。** 本文件只整理仓库内可证的同步相关事实，并给出一个离线可闭合的自有反例；
SDK 侧读写锁/票号辅助（`g_RoBusyMutex` 家族）的语义仍为未知。未执行真实游戏、SDK、原始 rtld 或手机；未改扫描器、
隔离区或辅助函数实现。

Status: **preparation and one owned counterexample, not an implementation or acceptance.** This document collects only
facts the repository can prove about synchronization, plus one gap that is closable offline. The SDK-side reader/writer
or ticket helper family (`g_RoBusyMutex`) remains unknown. No game, SDK, original rtld or phone was executed; the
scanner, quarantine and helper implementations are unchanged.

## 证据标签 | Evidence tags

| 标签 | 含义 |
|---|---|
| **[repo]** | 本仓库源码可证，附 `file:line`。 |
| **[ref]** | 外部一手资料（Arm 架构指南、公开内核参考），附链接。 |
| **[unknown]** | 仓库与公开资料都不能证明；不得用名称、AAPCS 或直觉填补。 |
| **[ours]** | 本切片新增的自有夹具或结论。 |

## 1. 参与者与地址 | Participants and addresses

- **[repo] 公开 main（`source snapshot`）的 `dm/` 源码树中没有 `g_RoBusyMutex`/`RoBusy` 命中**：
  `git grep -nE "g_RoBusyMutex|RoBusy" source snapshot -- dm` → 无命中。`ticket` 只出现在文档与出图队列注释中，例如
  `dm/android/README.md:273`、`dm/android/README.zh-CN.md:275`（只读调用方调查说明）与 `hle_nvn.c:838-845`
  （出图票号注释）；它们都不是该辅助函数的实现。该检索只覆盖公开文本，缺少具名实现 ≠ 缺少静态证据：
  SDK 二进制、计算跳转与外部调用者仍不可见。
- **[repo] 现有同步相关实现**：`dm/android/runtime/mutex.c`（Horizon 互斥/条件服务）、`runtime/arbiter.c`
  （WaitForAddress/SignalToAddress）、`runtime/sync.c`（事件）、`rewrite/exclusive.c`（独占窗口检查）。
- **[repo] SDK 侧只读调查记录**（非本仓库源码）：`dm/android/README.md:273` 说明存在一个 “native ticket-lock
  polling helper”，并称在三条已知直接调用者路径上看不到返回后继续使用预留；同一段明确说这**不覆盖**计算跳转或
  外部间接调用者。该记录是方法受限的静态观察，不是语义证明，也没有地址/字节可供本仓库复核。
- **[unknown]** 该 helper 的实际地址、ABI（返回类型、寄存器约定）、锁对象来源、别名关系、冷/热路径及所有调用者。

## 2. 保留状态与返回边界 | Reservation state and the return boundary

- **[ref]** AArch64 的独占监视器在明确清除事件后失效（`CLREX`、成功的 `STXR`、异常/上下文切换等）；普通函数调用与
  `RET` 本身不是清除事件。Arm A64 同步指南 §3.2.4（`dm/android/README.md:255` 引用同一来源）。
- **[repo]** 系统 SVC 桩在返回前显式执行 `CLREX`（`README.md:255`），并有合成反例：宿主门里重新 `ldxr` 预留，SVC
  桩返回后 `STXR` 必须失败且内存不变（`tests/system_patch.c:43-64`，`README.md:261`）。
- **[repo]** 独占窗口检查器拒绝带活预留的 `RET`/调用/间接跳转，只接受 `STXR`、`CLREX`、下一个独占加载或
  monitor-clearing SVC 边界（`rewrite/exclusive.h:7`；`rewrite/exclusive.c:28` 接受直接 SVC，
  `:35` 拒绝 `CALL/RET`，`:47` 限定 ±4 KiB 与 256 条指令）。
- **[ours]** 新反例 `dm/android/tests/exclusive_return.c`：`ldxr` 后直接 `RET` 的辅助函数返回后，调用方 `STXR`
  **成功**（64 次中 63 次；另有 1 次未观察到保留，原因未知——中断/调度/上下文切换等都可能清除，未建立因果）；对照组
  同形状但多一条 `CLREX`，`STXR` 64/64 失败且内存不变。结论：**调用者不能因为辅助函数“正常返回”或符合 AAPCS
  就认为预留已清除**；支持边界必须显式要求 `STXR`/`CLREX`/SVC 边界，或由检查器拒绝该路径。这是指令级反例，
  不是调度或通用保证。
- **[unknown]** SDK ticket helper 是否在执行 `STXR`/`CLREX` 后返回；其三条已知调用者之外的间接入口。

## 3. 低 16 位、票号回绕与满状态 | Low 16 bits, ticket wrap and full state

- **[repo]** 公开 Horizon 互斥服务使用 32 位字：`WAIT_MASK 0x40000000`（`runtime/mutex.c:8`），原子 load/store
  （`:21-22`），移交时写 tag|mask 并唤醒（`:51-60`）。它**没有** 16 位票号，也没有票号回绕/满状态语义。
- **[unknown]** SDK 辅助若使用低 16 位票号，其位域、回绕、满状态、溢出后的等待/拒绝行为均无仓库证据。不能把
  Horizon 互斥的 32 位字布局外推到它，也不能用函数名推断。

## 4. 字节/整字混合写入 | Mixed byte/word writes

- **[repo]** `mutex.c` 与 `arbiter.c` 只对 32 位字做原子操作（`mutex.c:21-22,103`；`arbiter.c:34,42,61-62`），
  没有字节/半字 CAS 或混合宽度写路径。
- **[unknown]** SDK helper 是否允许字节写与整字写并发、是否依赖单拷贝原子性（single-copy atomicity）或需要额外
  屏障；公开树无证据。

## 5. 等待/唤醒与错误路径 | Wait/wake and error paths

- **[repo]** 互斥/条件：等待者队列、优先级选择、移交与广播均在 `host->mutex` 下串行化，客体锁字使用
  `__ATOMIC_SEQ_CST`；超时使用单调时钟；句柄按代际校验（`runtime/mutex.c:8,21-22,51-60,78-105`）。
- **[repo]** 地址仲裁：谓词检查、入队与通知共享宿主锁以避免丢失唤醒；超时/错误返回
  `DM_SVC_BAD_STATE` 等 Result（`runtime/arbiter.c:34-62`）。
- **[unknown]** SDK helper 的等待/唤醒（若存在）是否使用同一套机制、错误路径是否回滚锁字、是否有假唤醒防御；
  本仓库的自有服务测试 **不** 构成该 helper 的证明。

## 6. 内存序与生命周期 | Memory order and lifetime

- **[repo]** 上述服务层原子操作全部 `__ATOMIC_SEQ_CST`；`CLREX`/独占访问本身不提供 acquire/release 语义，内存序由
  周边显式原子或屏障决定。
- **[repo]** 独占窗口内不得插入宿主调用（`rewrite/exclusive.h:7`，`README.md:255`）；宿主门只在首个独占加载前运行。
- **[unknown]** SDK helper 的 acquire/release 语义、背后内存生命周期、跨线程发布规则。

## 7. 内部入口支持边界 | Internal-entry support boundary

- **[repo]** 窗口解码与跳转表处理有保守边界：`rewrite/window_decode.c` 选择窗口内未用寄存器并验证替换；
  `rewrite/jump_table.c` 处理直接入口；间接入口与代码/数据分类未证明（`README.md:187-205`）。
- **[unknown]** SDK helper 的内部入口（热补丁、计算跳转、PLT 之外）是否被现有检查覆盖。

## 8. 本切片闭合的唯一缺口与剩余阻塞 | The one closed gap and remaining blockers

- **已闭合 [ours]**：返回边界反例证明 `RET` 不清理预留，从而否定“正常返回/AAPCS ⇒ 预留安全”的推断；支持边界必须是
  显式 `STXR`/`CLREX`/SVC 边界或检查器拒绝。夹具纯合成、无游戏数据。
- **仍阻塞**：低 16 位票号/回绕/满状态、混合宽度写、SDK helper 等待唤醒与内存序、未知间接调用者。闭合它们需要
  受控 SDK/设备侧证据授权（参数捕获与连续多帧对照），本切片不重复审计、不猜测。
- **重试条件**：获得可公开引用的 SDK 头文件说明，或经授权的受控运行捕获到实际参数/地址后，再建立子任务。

## 9. 复现 | Reproduction

```
cmake -S dm/android -B build/issue-4-host -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/issue-4-host --target dm_exclusive_return_test
build/issue-4-host/dm_exclusive_return_test
```

结果（Apple M4，合成 ARM64）：`ret_preserved=63/64 (cause-unknown non-survivals=1)`，
`clrex_blocked=64/64 unchanged=64/64`，exit 0。未能观察到保留的那一次原因未知，不断言每次成功。
非 AArch64 构建返回 77，CTest 以 `SKIP_RETURN_CODE 77` 记为 SKIP，不把“未执行指令”当成通过。
