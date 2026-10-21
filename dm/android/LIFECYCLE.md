# Android lifecycle ownership and pairing

This document is a bounded inventory of the Android host/module startup and
teardown paths. It does not claim that a real SDK constructor or destructor has
run. "Historical" refers to the previously consumed diagnostic prefix, not a new
execution. "Owned" means self-owned host/module tests in this tree.

| Owned stage / object | Acquire | Paired release / fini | Historical diagnostic reach | Current evidence | Status |
|---|---|---|---|---|---|
| Host reservation | `dm_host_create_*` | `dm_host_destroy` | Not an SDK stage | Owned accounting/module tests | Verified owned path |
| `_init_libc0` | SDK wrapper | No observed SDK destructor | Completed in consumed prefix | Static inventory only | Historical reach |
| `nnosInitialize` | `nnosInitialize` | `nnosQuickExit` is the later static exit edge | Completed in consumed prefix | Static only | Historical reach; exit unobserved |
| Owned loader-ready adapter | Adapter after `nnosInitialize` | No observed paired adapter teardown | Completed in consumed prefix | Owned/static code | Historical reach; not original rtld |
| `_init_libc1` | SDK wrapper | No observed SDK destructor | Completed in consumed prefix | Static only | Historical reach |
| `nninitInitializeSdkModule` | SDK module initializer | `nninitFinalizeSdkModule` is a bare `ret`; no body | Entered; stopped at unsupported `ConnectToNamedPort` SVC `0x1f`, result `0x4201`, guest exit 82 | No real or owned SDK-module execution | Partial historical reach; no return or cleanup proof |
| SDK init-array | Static init-array metadata | Matching fini entries are static-only | Not shown executed | Static only | Static-only; constructors not claimed executed |
| `nninitStartup` / `nnMain` | SDK startup / normal main | No observed SDK finalizer chain | Not reached | Static only | Static-only / unknown |
| `_init_libc2` | SDK wrapper | No observed paired destructor | Not reached | Static only | Static-only / unknown |
| `nninitFinalizeSdkModule` | Static finalizer symbol | Bare `ret`; no teardown body | Not reached | Static only | Static-only |
| `nnosQuickExit` | Static exit edge | Guest/process exit unobserved | Not reached | Static only | Static-only / unknown |
| Module mappings | `dm_host_map_modules` / `dm_modules_map` | `dm_modules_unmap`, host destroy | Not an SDK stage | Owned accounting: 11 commit failpoints, link-stage partial rollback, repeated map/destroy | Verified owned path |
| IPC ports, domain roots/children | `sm:` / `lm` / owned ports; conversion | Session/domain close, host destroy | Not proven through real endpoints | Owned IPC/SM/LM suites | Verified owned path |
| TLS/TLR local lease | Accounting local create / attach | Detach, accounting release | Not an SDK stage | Owned accounting: active lease refuses destroy with `EBUSY`; release then destroy succeeds | Verified owned path |
| Guest thread lease | Attach / `StartThread` | Detach, join, unmap | Not an SDK stage | Owned bootstrap/accounting/thread tests | Verified owned path |
| Memory pins | Pin under host lock | Unpin | Not an SDK stage | Owned accounting: destroy returns `EBUSY` while pinned | Verified owned path |
| Normal host exit | Detach guest | `dm_host_destroy` | Real SDK exit not reached | Owned detach then destroy; repeated cycles return accounting to the per-host baseline | Verified owned path |

## Remaining work

1. SDK module initializer reverse rollback is blocked on a reviewed real-service
   and guest-input contract. The historical prefix stopped inside
   `nninitInitializeSdkModule` at SVC `0x1f` with `0x4201`; no service/module
   reverse rollback occurred.
2. `nninitFinalizeSdkModule` has no observed body, and init/fini arrays are
   static-only. Executing them requires a separately approved real-input task;
   a synthetic smoke path or success return is not a substitute.
3. Duplicate session/domain close is covered by owned IPC tests. Double
   `dm_host_destroy` is not a supported API contract.
4. Active threads, pins, aliases and accounting leases keep destruction at
   `EBUSY`; the caller must release them in reverse order.
