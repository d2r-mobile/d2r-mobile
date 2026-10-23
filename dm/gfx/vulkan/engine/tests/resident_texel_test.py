# 驻留 texel 淘汰、代际及槽生命周期模型，依据 PR10b 的独立驻留设计。
# Resident texel eviction, generation and slot-lifetime model based on the PR10b resident design.
from pathlib import Path
from texture_cache_regression import balanced
s=(Path(__file__).resolve().parent.parent/'xbg_game.cpp').read_text(encoding='utf-8')
a=balanced(s,s.index('TexelResident *texel_acquire('))
assert a.index('for(auto &w:pending_writebacks)if(xbg_wb_overlaps(base,bytes,w.addr,w.bytes))')<a.index('it->second.checked==slot_serial')
assert 'slots[si].recording && !slots[si].pending' in a
assert 'texel_live_bytes[si]+texel_hole_bytes[si]+bytes>64ull*1024*1024' in a
assert 'if(r.used==slot_serial)' in a
assert 'r.gen=++texel_gen' in a
v=balanced(s,s.index('void texel_evict(unsigned si,bool all){'))
assert v.index('!slots[si].pending')<v.index('vkDestroyBufferView')
assert v.index('vkDestroyBufferView')<v.index('texel_arena[si].release()')
assert 'r->gen!=tr.gen' in s and 'desc_memo_texel_fail' in s
class Slot:
    def __init__(self): self.pending=False;self.map={};self.gen=0;self.holes=0;self.released=0
    def acquire(self,key,serial,content):
        assert not self.pending
        old=self.map.get(key)
        if old and old[2]!=content and old[1]==serial: return None
        if not old: self.gen+=1
        gen=old[0] if old else self.gen
        self.map[key]=(gen,serial,content)
        return gen
    def evict(self,serial,all=False):
        assert not self.pending
        for key,(gen,used,content) in list(self.map.items()):
            if all or used+128<serial: del self.map[key];self.holes+=key[1]
        if all or self.holes>8*1024*1024:self.map.clear();self.holes=0;self.released+=1
slots=[Slot(),Slot()]
for n in range(1,1025):
    slot=slots[n%2];slot.pending=False;slot.evict(n)
    key=(n%67,256*1024)
    gen=slot.acquire(key,n,n)
    assert slot.acquire(key,n,n)==gen
    assert slot.acquire(key,n,n+1) is None
    slot.pending=True
    try:slot.evict(n,True)
    except AssertionError:pass
    else:raise AssertionError('in-flight eviction accepted')
for slot in slots:
    slot.pending=False;old=slot.gen;slot.evict(2048,True)
    assert not slot.map and slot.holes==0
    assert slot.acquire((1,4096),2049,7)>old
    assert slot.released>1
print('PASS: 1024 two-slot retire/reuse/eviction/arena-release model cycles and source guards; Vulkan driver not executed')
