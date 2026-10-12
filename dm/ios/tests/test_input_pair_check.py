import importlib.util,unittest,os
from pathlib import Path
P=Path(os.environ.get('PAIR_SOURCE',str(Path(__file__).resolve().parents[1]/'tools/input_pair_check.py')))
spec=importlib.util.spec_from_file_location('pair',P);pair=importlib.util.module_from_spec(spec);spec.loader.exec_module(pair)
def state(lx=0):return dict(buttons=0,lx=lx,ly=0,rx=0,ry=0)
def trace(axis=32767):
 ui=[];hid=[]
 for i,(phase,value) in enumerate([('began',0),('moved',axis),('ended',0)],1):
  n=i*100;v=state(value)
  ui.append(dict(stage='event',eventseq=i,mono_ns=n,source='uitouch',phase=phase,slot=0,touch='0x1',x=90+value/32767*60,y=200,touch_timestamp_s=42.125,mode='game',**v))
  ui.append(dict(stage='output',receiptseq=i,eventseq=i,event_first_seq=i,event_last_seq=i,mono_ns=n+10,publish_end_ns=n+20,source='uitouch',phase=phase,mode='game',**v))
  hid.append(dict(event='npad_return',read_start_ns=n+25,time_ns=n+30,sampling=i,id=32,style=2,attributes=1,script_mask=0,**v))
 return ui,hid
class PairFixtureTests(unittest.TestCase):
 def case(self,f,status,reason=None):
  u,h=trace();f(u,h);g=pair.analyze(u,h,domain='synthetic')
  self.assertEqual(g['status'],status,g);self.assertFalse(g['physical_pass_claimed'],g)
  if reason:self.assertIn(reason,g['reasons'],g)
 def test_01_positive_right(self):self.case(lambda u,h:None,'protocol_pass')
 def test_02_positive_left(self):
  u,h=trace(-32767);g=pair.analyze(u,h);self.assertEqual(g['status'],'protocol_pass');self.assertFalse(g['physical_pass_claimed'])
 def test_03_release(self):
  u,h=trace();g=pair.analyze(u,h);self.assertEqual(g['status'],'protocol_pass');self.assertTrue(g['release_received'])
 def test_04_stale_equal(self):self.case(lambda u,h:h[1].update(read_start_ns=1,time_ns=2),'incomplete','missing_receipt')
 def test_05_invalid_controller_zero(self):self.case(lambda u,h:h[-1].update(id=99,style=0,attributes=0),'incomplete','invalid_controller')
 def test_06_non_touch_output(self):
  def f(u,h):
   u[:]=[dict(stage='output',receiptseq=1,eventseq=0,event_first_seq=0,event_last_seq=0,mono_ns=110,publish_end_ns=120,source='layout',phase='none',mode='game',**state(32767))]
   h[:]=[dict(event='npad_return',read_start_ns=125,time_ns=130,sampling=1,id=32,style=2,attributes=1,script_mask=0,**state(32767))]
  self.case(f,'incomplete','no_touch_pairs')
 def test_07_blank_no_receipt(self):self.case(lambda u,h:h.clear(),'incomplete','missing_receipt')
 def test_08_missing_release(self):self.case(lambda u,h:(u.__delitem__(slice(-2,None)),h.pop()),'incomplete','lifecycle_unclosed')
 def test_09_blank_activity_witness(self):
  u,h=trace(0);u[0]['blank']=True;h[1].update(sampling=26);h[2].update(sampling=27);g=pair.analyze(u,h);self.assertEqual(g['status'],'protocol_pass');self.assertTrue(g['blank_activity_received'])
 def test_10_script_confounded(self):self.case(lambda u,h:h[1].update(script_mask=33554432),'incomplete','script_confounded')
 def test_11_cap_reached(self):self.case(lambda u,h:h.append(dict(event='limit_reached',limit=1024)),'incomplete','limit_reached')
 def test_12_read_crosses_publish(self):self.case(lambda u,h:h[1].update(read_start_ns=215,time_ns=225),'incomplete','concurrent_undetermined')
 def test_13_stable_read_after_publish(self):self.case(lambda u,h:h[1].update(read_start_ns=230,time_ns=250),'protocol_pass')
 def test_14_duplicate_sampling(self):self.case(lambda u,h:h[1].update(sampling=1),'incomplete','sampling_not_forward')
 def test_15_fractional_integer(self):self.case(lambda u,h:h[1].update(lx=32767.9),'invalid_trace','bad_integer')
 def test_16_group_tail(self):self.case(lambda u,h:u[3].update(event_last_seq=1),'invalid_trace','event_group_not_closed')
 def test_17_no_dual_stream(self):
  u,h=trace();g=pair.analyze(u,None);self.assertEqual(g['status'],'pair_not_executed');self.assertFalse(g['physical_pass_claimed'])
 def test_18_state_mismatch(self):self.case(lambda u,h:h[1].update(lx=100),'incomplete','returned_state_mismatch')
 def test_19_device_domain_not_physical(self):
  u,h=trace();g=pair.analyze(u,h,domain='device');self.assertEqual(g['status'],'protocol_pass');self.assertFalse(g['physical_pass_claimed'])
 def test_20_slot_transition(self):self.case(lambda u,h:u[2].update(slot=1),'invalid_trace','slot_transition')
 def test_21_touch_timestamp_not_clock(self):
  u,h=trace();u[2]['touch_timestamp_s']=1e20;g=pair.analyze(u,h);self.assertEqual(g['status'],'protocol_pass')
 def test_22_release_not_neutral(self):
  def f(u,h):u[-1]['lx']=100;h[-1]['lx']=100
  self.case(f,'incomplete','release_not_neutral')
 def test_23_blank_unwanted_A(self):
  def f(u,h):u[0]['blank']=True;u[1]['buttons']=1;h[0]['buttons']=1
  self.case(f,'incomplete','blank_unwanted_A')
 def test_24_read_crosses_next_publish(self):self.case(lambda u,h:h[1].update(read_start_ns=250,time_ns=315),'incomplete','concurrent_undetermined')
 def test_25_unsigned_attribute_corruption(self):self.case(lambda u,h:h[1].update(attributes=-1),'invalid_trace','unsigned_range')
 def test_26_read_start_equals_publish_end(self):
  self.case(lambda u,h:h[1].update(read_start_ns=u[3]['publish_end_ns']),'incomplete','concurrent_undetermined')
 def test_27_matched_int32_overflow(self):
  def f(u,h):u[3]['lx']=2**31;h[1]['lx']=2**31
  self.case(f,'invalid_trace','int32_range')
 def binding_trace(self):
  u,h=trace();v=dict(buttons=256,lx=0,ly=0,rx=0,ry=0)
  u.insert(4,dict(stage='output',receiptseq=3,eventseq=2,event_first_seq=0,event_last_seq=2,mono_ns=260,publish_end_ns=270,source='bindings',phase='none',mode='game',**v))
  u[-1]['receiptseq']=4
  h.insert(2,dict(event='npad_return',read_start_ns=275,time_ns=280,sampling=3,id=32,style=2,attributes=1,script_mask=0,**v));h[-1]['sampling']=4
  return u,h
 def test_28_bindings_output_not_new_touch(self):
  u,h=self.binding_trace();g=pair.analyze(u,h)
  self.assertEqual(g['status'],'protocol_pass',g);self.assertEqual(g['touch_output_pairs'],3)
  self.assertEqual(g['pairs'][2]['attribution'],'output_only_not_new_touch')
  self.assertEqual(g['pairs'][2]['matched_sampling'],[3]);self.assertFalse(g['physical_pass_claimed'])
 def test_29_bindings_only_cannot_claim_touch(self):
  u,h=self.binding_trace();u=[u[4]];u[0].update(receiptseq=1,eventseq=0,event_first_seq=0,event_last_seq=0);h=[h[2]]
  g=pair.analyze(u,h);self.assertEqual(g['status'],'incomplete',g);self.assertIn('no_touch_pairs',g['reasons'])
 def test_30_bindings_cannot_be_touch_event(self):
  u,h=trace();u[0]['source']='bindings';g=pair.analyze(u,h)
  self.assertEqual(g['status'],'invalid_trace');self.assertIn('bad_event_source_phase',g['reasons'])
 def test_31_bindings_must_use_none_phase(self):
  u,h=self.binding_trace();u[4]['phase']='began';g=pair.analyze(u,h)
  self.assertEqual(g['status'],'invalid_trace');self.assertIn('bad_output_phase',g['reasons'])
 def test_32_unknown_program_source_still_rejected(self):
  u,h=self.binding_trace();u[4]['source']='unfrozen_future_source';g=pair.analyze(u,h)
  self.assertEqual(g['status'],'invalid_trace');self.assertIn('bad_source_or_mode',g['reasons'])
 def portal_trace(self,scope=2):
  base=dict(portal_request=7,portal_binding=8,portal_scene=9,portal_phase=1,portal_pending=1,modifier_publish_end_ns=120,feedback_group_start_ns=50,feedback_captured_ns=80,feedback_secondary=0,feedback_seen=63,feedback_identity_scope=scope)
  def ev(i,n,phase,buttons):return dict(stage='event',eventseq=i,mono_ns=n,source='uitouch',phase=phase,slot=0,touch='0x1',x=300,y=150,touch_timestamp_s=42,mode='game',**dict(state(),buttons=buttons))
  def out(i,e,n,phase,source,buttons,first,extra):return dict(stage='output',receiptseq=i,eventseq=e,event_first_seq=first,event_last_seq=e,mono_ns=n,publish_end_ns=n+10,source=source,phase=phase,mode='game',**dict(state(),buttons=buttons),**dict(base,**extra))
  u=[ev(1,100,'began',256),out(1,1,110,'began','uitouch',256,1,{}),out(2,1,200,'none','portal_sequence',258,0,dict(portal_phase=2,portal_pending=0,feedback_group_start_ns=150,feedback_captured_ns=180,feedback_secondary=1)),ev(2,300,'ended',0),out(3,2,310,'ended','uitouch',0,2,dict(portal_request=8,portal_phase=0,portal_pending=0,modifier_publish_end_ns=0))]
  h=[dict(event='npad_return',read_start_ns=n+15,time_ns=n+20,sampling=i,id=32,style=2,attributes=1,script_mask=0,**dict(state(),buttons=b))for i,n,b in [(1,110,256),(2,210,258),(3,310,0)]]
  return u,h
 def portal_case(self,change,status,reason,domain='synthetic'):
  u,h=self.portal_trace();change(u,h);g=pair.analyze(u,h,domain)
  self.assertEqual(g['status'],status,g);self.assertIn(reason,g['reasons'],g);self.assertFalse(g['physical_pass_claimed'])
 def test_33_portal_combo_actual_two_receipts(self):
  u,h=self.portal_trace();g=pair.analyze(u,h);self.assertEqual(g['status'],'protocol_pass',g)
  self.assertTrue(g['combo_received']);self.assertEqual(g['portal_combo_pairs'],1);self.assertEqual(g['touch_output_pairs'],2)
  self.assertEqual(g['pairs'][1]['attribution'],'output_only_not_new_touch');self.assertTrue(g['release_received']);self.assertFalse(g['physical_pass_claimed'])
 def test_34_portal_no_combo_read(self):self.portal_case(lambda u,h:h.pop(1),'incomplete','missing_combo_receipt')
 def test_35_portal_no_modifier_read(self):self.portal_case(lambda u,h:h.pop(0),'incomplete','missing_modifier_receipt')
 def test_36_portal_tied_combo_read(self):self.portal_case(lambda u,h:h[1].update(read_start_ns=210),'incomplete','concurrent_undetermined')
 def test_37_portal_old_group_start(self):self.portal_case(lambda u,h:u[2].update(feedback_group_start_ns=120),'invalid_trace','portal_feedback_time')
 def test_38_portal_future_feedback(self):self.portal_case(lambda u,h:u[2].update(feedback_captured_ns=200),'invalid_trace','portal_feedback_time')
 def test_39_portal_incomplete_group(self):self.portal_case(lambda u,h:u[2].update(feedback_seen=31),'invalid_trace','portal_feedback_identity')
 def test_40_portal_unknown_identity(self):self.portal_case(lambda u,h:u[2].update(feedback_identity_scope=0),'invalid_trace','portal_feedback_identity')
 def test_41_portal_synthetic_identity_device(self):self.portal_case(lambda u,h:None,'invalid_trace','synthetic_identity_on_device',domain='device')
 def test_42_portal_ack_after_release(self):
  def change(u,h):
   ack=u.pop(2);ack.update(mono_ns=400,publish_end_ns=410,receiptseq=3,eventseq=2,event_first_seq=0,event_last_seq=2);u[-1]['receiptseq']=2;u.append(ack)
  self.portal_case(change,'invalid_trace','portal_request_not_pending')
 def test_43_portal_binding_changed(self):self.portal_case(lambda u,h:u[2].update(portal_binding=10),'invalid_trace','portal_request_identity')
 def test_44_portal_scene_changed(self):self.portal_case(lambda u,h:u[2].update(portal_scene=10),'invalid_trace','portal_request_identity')
 def test_45_portal_request_changed(self):self.portal_case(lambda u,h:u[2].update(portal_request=10),'invalid_trace','portal_request_identity')
 def test_46_portal_duplicate_ack(self):
  def change(u,h):
   ack=dict(u[2],mono_ns=250,publish_end_ns=260,receiptseq=3);u.insert(3,ack);u[-1]['receiptseq']=4
  self.portal_case(change,'invalid_trace','portal_request_not_pending')
 def test_47_portal_source_is_not_event(self):self.portal_case(lambda u,h:u[0].update(source='portal_sequence'),'invalid_trace','bad_event_source_phase')
 def test_48_portal_field_fraction(self):self.portal_case(lambda u,h:u[2].update(portal_scene=9.5),'invalid_trace','bad_integer')
 def test_49_portal_field_negative(self):self.portal_case(lambda u,h:u[2].update(portal_binding=-1),'invalid_trace','portal_unsigned_range')
 def test_50_portal_missing_feedback_field(self):self.portal_case(lambda u,h:u[2].pop('feedback_captured_ns'),'invalid_trace','missing_fields:feedback_captured_ns')
 def test_51_portal_wrong_modifier_publication(self):self.portal_case(lambda u,h:u[1].update(modifier_publish_end_ns=121),'invalid_trace','portal_modifier_publication')
 def test_52_portal_not_ZL_prefix(self):self.portal_case(lambda u,h:u[1].update(buttons=2),'invalid_trace','portal_modifier_publication')
 def test_53_portal_no_action_edge(self):self.portal_case(lambda u,h:u[2].update(buttons=256),'invalid_trace','portal_action_edge')
 def test_54_portal_wrong_action_phase(self):self.portal_case(lambda u,h:u[2].update(portal_pending=1),'invalid_trace','portal_request_not_pending')
 def test_55_portal_navigation_ack(self):self.portal_case(lambda u,h:u[2].update(mode='navigation'),'invalid_trace','portal_request_not_pending')
 def test_56_portal_stale_feedback(self):self.portal_case(lambda u,h:u[2].update(mono_ns=250000181,publish_end_ns=250000191),'invalid_trace','portal_feedback_time')
 def test_57_portal_single_ui_still_not_executed(self):
  u,h=self.portal_trace();g=pair.analyze(u,None);self.assertEqual(g['status'],'pair_not_executed',g);self.assertFalse(g['combo_received'])
 def test_58_portal_native_identity_still_not_physical(self):
  u,h=self.portal_trace(scope=1);g=pair.analyze(u,h,domain='device');self.assertEqual(g['status'],'protocol_pass',g);self.assertTrue(g['combo_received']);self.assertFalse(g['physical_pass_claimed'])
 def test_59_portal_action_without_ack(self):
  def change(u,h):
   row=dict(u[1],receiptseq=2,event_first_seq=0,mono_ns=160,publish_end_ns=170,source='screenshot',phase='none',portal_phase=2,portal_pending=0,buttons=258);u.insert(2,row);u[3]['receiptseq']=3;u[-1]['receiptseq']=4
  self.portal_case(change,'invalid_trace','portal_action_without_ack')
 def test_60_portal_touch_cleared_before_ack(self):
  def change(u,h):
   row=dict(u[1],receiptseq=2,event_first_seq=0,mono_ns=160,publish_end_ns=170,source='layout',phase='none');u.insert(2,row);u[3]['receiptseq']=3;u[-1]['receiptseq']=4;u[-2]['slot']=-1
  self.portal_case(change,'invalid_trace','portal_touch_not_active')
 def test_61_portal_boolean_identity(self):self.portal_case(lambda u,h:u[2].update(portal_binding=True),'invalid_trace','boolean_not_integer')
 def test_62_portal_combo_drops_modifier(self):self.portal_case(lambda u,h:u[2].update(buttons=2),'invalid_trace','portal_action_edge')
 def test_63_portal_combo_ambiguous_edge(self):self.portal_case(lambda u,h:u[2].update(buttons=259),'invalid_trace','portal_action_edge')
 def test_64_modifier_read_after_feedback_is_not_causal(self):
  u,h=self.portal_trace();h[0].update(read_start_ns=185,time_ns=190);g=pair.analyze(u,h)
  self.assertEqual(g['status'],'incomplete',g);self.assertIn('modifier_not_before_feedback',g['reasons']);self.assertFalse(g['combo_received'])
 def test_65_modifier_read_equal_feedback_start_undetermined(self):
  u,h=self.portal_trace();h[0].update(read_start_ns=145,time_ns=150);g=pair.analyze(u,h)
  self.assertEqual(g['status'],'incomplete',g);self.assertIn('modifier_not_before_feedback',g['reasons']);self.assertFalse(g['combo_received'])
 def two_portal_requests(self,second):
  u,h=self.portal_trace();u2,h2=self.portal_trace()
  for row in u2:
   row['mono_ns']+=300;row['eventseq']+=2
   if row['stage']=='event':row['touch']='0x2'
   else:
    row['receiptseq']+=3;row['publish_end_ns']+=300;row['event_last_seq']+=2
    if row['event_first_seq']:row['event_first_seq']+=2
    row['portal_request']=second+(row['portal_phase']==0)
    for k in ['modifier_publish_end_ns','feedback_group_start_ns','feedback_captured_ns']:
     if row[k]:row[k]+=300
  for row in h2:row['read_start_ns']+=300;row['time_ns']+=300;row['sampling']+=3
  return u+u2,h+h2
 def test_66_two_requests_reuse_old_epoch_rejected(self):
  u,h=self.two_portal_requests(7);g=pair.analyze(u,h)
  self.assertEqual(g['status'],'invalid_trace',g);self.assertIn('portal_request_regression',g['reasons']);self.assertFalse(g['combo_received'])
 def test_67_two_requests_forward_epoch_valid(self):
  u,h=self.two_portal_requests(9);g=pair.analyze(u,h)
  self.assertEqual(g['status'],'protocol_pass',g);self.assertEqual(g['portal_combo_pairs'],2);self.assertTrue(g['combo_received']);self.assertFalse(g['physical_pass_claimed'])
if __name__=='__main__':unittest.main()
