#!/usr/bin/env python3
from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[1]
# 这里原有 10 条「在 front_ios.m / DmTouchOverlay.m 里找某段源码文字」的断言（回城键绑定的刷新时机、字段名等），产品重构后对不上。
# 它们守的协议行为现在由 C 单测 t_binding_freshness / t_portal_sequence / t_binding_group / t_native_binding 守，端到端由冒烟的触摸玩家路径和回归整表守——删掉。
# 留下的是数据一致性：历版触屏布局规格文档冻结不动、现行版的哈希与编进程序的常量一致。
# There used to be 10 assertions here that "look for a piece of source text in front_ios.m / DmTouchOverlay.m" (refresh timing of the town portal key binding, field names, etc.), which no longer matched after the product refactor.
# The protocol behavior they guarded is now guarded by the C unit tests t_binding_freshness / t_portal_sequence / t_binding_group / t_native_binding, and end to end by the smoke test's touch player path and the full regression table - deleted.
# What remains is data consistency: the touch layout spec documents of past versions stay frozen, and the current version's hash matches the constant compiled into the program.
old=root/'docs/touch_layout_spec.json'
assert hashlib.sha256(old.read_bytes()).hexdigest()=='38f3fed9acc16276ce278ae1b05ea7dad979e19cac53c447c4b73f014c3cb606'
previous=root/'docs/touch_layout_spec_native_binding_v2.json'
assert hashlib.sha256(previous.read_bytes()).hexdigest()=='2209cb320184d9ed58cd9fbee72dac734333773ff3332882405dba889749b13b'
previous3=root/'docs/touch_layout_spec_native_binding_v3.json'
assert hashlib.sha256(previous3.read_bytes()).hexdigest()=='c2921d0ef1bba260c30134603d4c2cbee50235483fa4b261e62069ef5cf9e3bb'
previous4=root/'docs/touch_layout_spec_native_binding_v4.json'
assert hashlib.sha256(previous4.read_bytes()).hexdigest()=='048ee8bfffa348d6a6829feb394ce723ad6acf892447d051eb7073b863a32d2f'
new=root/'docs/touch_layout_spec_native_binding_v5.json'
data=json.loads(new.read_text());assert data['profile_id']=='touch_native_binding_v5'
assert hashlib.sha256(new.read_bytes()).hexdigest() in (root/'Sources/dm_touch_native_art.c').read_text()
print('PASS frozen v1/v2/v3/v4 spec hashes; v5 profile/spec hash matches compiled constant')
