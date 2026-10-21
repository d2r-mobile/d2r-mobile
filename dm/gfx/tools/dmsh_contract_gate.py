#!/usr/bin/env python3
# dmsh_build.py 的显式产物闸门（DMSH_CONTRACT=1 才启用）：把 dmsh_spirv_contract 的契约检查接到真实出包路径上。
# 职责只有三件：① 翻译器产出的 <key>.spv / <key>.res.json 在 spirv-cross 编译与发布之前必须成对存在并通过契约；
# ② 缓存条目只有在元数据里记着「同一闸门版本」验证过时才可复用（旧条目、未验模式出的条目一律重出）；
# ③ 被拒的键不发布、不覆盖已发布产物，构建以非 0 退出。这里不实现新的缓存框架，也不做完整 SPIR-V 校验。
# Explicit artifact gate for dmsh_build.py (only active with DMSH_CONTRACT=1): wires dmsh_spirv_contract into the real
# build path. Three jobs only: (1) the translator's <key>.spv / <key>.res.json must exist as a pair and pass the contract
# before spirv-cross compiles and before publishing; (2) a cache entry may only be reused when its metadata records
# validation by this same gate version (old entries and entries published without the gate are rebuilt); (3) a rejected key
# is not published, already-published outputs are left alone, and the build exits non-zero. No new cache framework, and this
# is not a full SPIR-V validator.
import hashlib, json, os

import dmsh_spirv_contract as contract

# 目标 ABI：闸门校验的是 Vulkan 目标下的 set 0 契约（SPIR-V 1.0–1.3）。改了契约检查或目标就换 id，旧缓存自然失效。
# Target ABI: the gate validates the Vulkan-target set-0 contract (SPIR-V 1.0-1.3). Changing the checks or the target
# changes the id, so old cache entries stop being reusable.
CONTRACT_ABI = "vulkan-set0/spirv1.0-1.3"


def contract_id():
    """闸门版本 = 契约检查源码 + 本模块源码 + 目标 ABI 的哈希（前 16 位）。 | Gate version = hash of the contract checks,
    this module and the target ABI (first 16 hex chars)."""
    h = hashlib.sha256()
    for path in (getattr(contract, "__file__", ""), os.path.abspath(__file__)):
        with open(path, "rb") as f:
            h.update(f.read())
        h.update(b"\0")
    h.update(CONTRACT_ABI.encode())
    return h.hexdigest()[:16]


def _empty(path):
    try:
        return os.path.getsize(path) == 0
    except OSError:
        return True


def validate_pair(key, spv, resp, want_local=None):
    """校验一对翻译器产物。返回 (ok, 原因)；原因里带键，便于按失败键定位。
    Validate one translator output pair. Returns (ok, reason) with the key in the reason for per-key triage."""
    if not os.path.exists(spv):
        return False, "%s：SPIR-V 产物缺失（%s）" % (key, os.path.basename(spv))
    if not os.path.exists(resp):
        return False, "%s：反射产物缺失（%s）——翻译器只出了一半，按失败处理" % (key, os.path.basename(resp))
    if _empty(spv) or _empty(resp):
        return False, "%s：产物为空文件（%s/%s）" % (key, os.path.basename(spv), os.path.basename(resp))
    try:
        contract.check_pair(spv, resp, want_local)
    except contract.Violation as e:
        return False, "%s：%s" % (key, e)
    except Exception as e:   # 输入形态意外也不静默放行 | unexpected shapes must not pass silently
        return False, "%s：闸门内部错误 %s: %s" % (key, type(e).__name__, e)
    return True, ""


def record_usable(meta_path, gate_id):
    """复用判定第一步：发布元数据必须记着同一闸门版本的验证记录。翻译器的 .spv/.res.json 不进缓存，命中时无法重验源产物，
    所以没有记录的条目按「不可复用」重出，而不是放行。
    First reuse check: the publish metadata must record validation by this same gate version. The translator's
    .spv/.res.json is not cached, so a hit cannot re-check the source pair: an entry without the record is rebuilt, never
    waved through."""
    try:
        meta = json.load(open(meta_path, encoding="utf-8"))
    except (OSError, ValueError):
        return False, "无缓存元数据"
    if meta.get("contract") != gate_id:
        return False, "缓存条目没有同一闸门的验证记录（旧条目或未经闸门发布）"
    return True, ""


def published_usable(refl_path, key, chain):
    """复用判定第二步：已发布的 <key>_b.json 必须仍是本键、本链、已知阶段且来自 B 路线（损坏或串键的条目重出）。
    Second reuse check: the published <key>_b.json must still be this key, this chain, a known stage and route B (corrupt
    or crossed entries are rebuilt)."""
    try:
        refl = json.load(open(refl_path, encoding="utf-8"))
    except (OSError, ValueError):
        return False, "已发布反射缺失或损坏"
    if refl.get("route") != "B" or refl.get("dm_ctl") != key or refl.get("chain") != chain:
        return False, "已发布反射的键/链/路线不符（%r/%r/%r）" % (refl.get("dm_ctl"), refl.get("chain"), refl.get("route"))
    if refl.get("stage") not in ("vs", "ps", "cs"):
        return False, "已发布反射的阶段 %r 不在已知集合" % (refl.get("stage"),)
    return True, ""


def stamp(meta, gate_id, pair=None, published=None):
    """发布元数据加上验证记录（新 dict，不改入参）。pair / published 是源产物对与已发布产物的内容摘要，缓存命中时据此重验。
    Return a copy of the publish metadata carrying the validation record; pair / published hold content digests of the
    source pair and the published outputs that a cache hit is re-validated against."""
    out = dict(meta or {})
    out["contract"] = gate_id
    if pair:
        out["pair"] = dict(pair)
    if published:
        out["published"] = dict(published)
    return out


def file_sha(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def pair_digests(spv, resp):
    """源产物对的 sha256（保留原字节，不解析）。 | sha256 of the source pair (raw bytes, no parsing)."""
    return {"spv": file_sha(spv), "res": file_sha(resp)}


def retained_pair_usable(root, key, meta):
    """留存源产物对的内容校验：文件在、摘要与元数据记录一致、再跑一遍契约。改动一个字节（哪怕 JSON 仍合法、
    键/阶段标签不变）都会因摘要不符被拒。
    Content check for the retained source pair: files present, digests equal to the recorded ones, and the contract holds
    again. A single altered byte - even with valid JSON and unchanged key/stage labels - fails the digest check."""
    pair = (meta or {}).get("pair") or {}
    spv = os.path.join(root, "src", key + ".spv")
    resp = os.path.join(root, "src", key + ".res.json")
    for label, path in (("spv", spv), ("res", resp)):
        if not os.path.exists(path):
            return False, "缓存里没有留存的源产物 %s（旧条目或未留存）" % label
        if _empty(path):
            return False, "留存的源产物 %s 是空文件" % label
        want = pair.get(label)
        if not want:
            return False, "发布记录里没有 %s 的内容摘要" % label
        got = file_sha(path)
        if got != want:
            return False, "留存的源产物 %s 内容与发布时记录的摘要不符（被改动过）" % label
    try:
        contract.check_pair(spv, resp)
    except contract.Violation as e:
        return False, "留存源产物对不再满足契约：%s" % e
    except Exception as e:   # 输入形态意外也不放行 | unexpected shapes must not pass
        return False, "留存源产物对校验内部错误 %s: %s" % (type(e).__name__, e)
    return True, ""


def candidate_usable(meta_path, root, key, chain, gate_id):
    """缓存候选（还在 CAS 里、未进公共输出目录）的完整复用判定：验证记录 + 留存源产物对内容重验 + 已发布反射自洽。
    任何一步不过就重出，且绝不先恢复再验。
    Full reuse decision for a cache candidate (still in the CAS, not yet in the public output dir): validation record,
    re-validation of the retained source pair by content, and the published reflection's self-consistency. Any failure means
    rebuild, and validation always happens before restore."""
    ok, why = record_usable(meta_path, gate_id)
    if not ok:
        return False, why
    try:
        meta = json.load(open(meta_path, encoding="utf-8"))
    except (OSError, ValueError):
        return False, "无缓存元数据"
    ok, why = retained_pair_usable(root, key, meta)
    if not ok:
        return False, why
    published = meta.get("published") or {}
    if not published:
        return False, "发布记录里没有已发布产物的内容摘要（旧条目或未留存）"
    for name, want in published.items():
        path = os.path.join(root, "mac", name)
        if not os.path.exists(path):
            return False, "缓存里没有已发布产物 %s" % name
        if file_sha(path) != want:
            return False, "已发布产物 %s 的内容与发布时记录的摘要不符（被改动过）" % name
    return published_usable(os.path.join(root, "mac", key + "_b.json"), key, chain)
