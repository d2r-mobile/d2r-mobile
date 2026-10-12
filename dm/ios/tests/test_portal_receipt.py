from pathlib import Path
import tempfile
from test_touch_receipt import check_log
base='[touch-receipt] stage=output receiptseq=1 eventseq=0 event_first_seq=0 event_last_seq=0 mono_ns=200 publish_end_ns=201 source=portal_sequence phase=none buttons=0x102 lx=0 ly=0 rx=0 ry=0 mode=game'
checks=0
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'receipt.log'
 p.write_text(base+'\n')
 try:check_log(p)
 except AssertionError:checks+=1
 else:raise AssertionError('portal_sequence must carry request/binding/scene/phase/pending/modifier timestamp')
 good=base+' portal_request=5 portal_binding=77 portal_scene=3 portal_phase=2 portal_pending=0 modifier_publish_end_ns=100 feedback_group_start_ns=110 feedback_captured_ns=180 feedback_secondary=1 feedback_seen=63 feedback_identity_scope=2'
 p.write_text(good+'\n');check_log(p);checks+=1
 for old,new in [('portal_request=5','portal_request=0'),('portal_pending=0','portal_pending=1'),('portal_phase=2','portal_phase=1'),('modifier_publish_end_ns=100 feedback_group_start_ns=110 feedback_captured_ns=180 feedback_secondary=1 feedback_seen=63 feedback_identity_scope=2','modifier_publish_end_ns=200'),('buttons=0x102','buttons=0x100'),('feedback_seen=63','feedback_seen=1'),('feedback_identity_scope=2','feedback_identity_scope=0'),('feedback_group_start_ns=110','feedback_group_start_ns=99')]:
  p.write_text(good.replace(old,new)+'\n')
  try:check_log(p)
  except AssertionError:checks+=1
  else:raise AssertionError('negative accepted: '+new)
print(f'PASS portal_sequence receipt typed-phase/time/request negative {checks}/{checks} checks')
