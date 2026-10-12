#!/usr/bin/env python3
"""诊断收据契约：源接线检查及独立 transcript 判据，不把 STUB 当 HLE。 | Diagnostic receipt contract: source wiring checks and an independent transcript criterion, STUB is not treated as HLE."""
import argparse
import math
import re
from pathlib import Path

SOURCES = {'uitouch', 'navigation', 'layout', 'focus_cancel', 'screenshot', 'bindings', 'portal_sequence'}
TOUCH_PHASES = {'began', 'moved', 'ended', 'cancelled'}


def check_source(path):
    s = Path(path).read_text()
    assert '[touch-receipt]' in s, 'missing touch-receipt diagnostic records'
    assert 'getenv("DM_TOUCH_CONTROL_LOG")' in s, 'missing environment gate'
    assert '"cancelled"' in s, 'cancelled must not be mislabeled ended'
    for source in ('uitouch', 'layout', 'navigation', 'focus_cancel', 'screenshot', 'bindings', 'portal_sequence'):
        assert f'"{source}"' in s, f'missing source={source}'
    assert s.count('++_touchEventSeq') == 1, 'event sequence has one UIKit-only increment site'
    assert 'slot=-1' in s or 'int slot=-1' in s, 'unowned physical touch must remain observable'
    assert 'touch_timestamp_s=' in s and 'touch.timestamp' in s, 'missing original UITouch timestamp'
    assert 'event_first_seq=' in s and 'event_last_seq=' in s, 'missing publication event group'
    print('SOURCE PASS: env gate + 4 phases + 7 sources + UIKit-only eventseq + unowned physical point + raw timestamp + publication event group')


def check_log(path):
    records = []
    for line in Path(path).read_text(errors='replace').splitlines():
        if '[touch-receipt]' not in line:
            continue
        pairs = dict(re.findall(r'(\w+)=([^\s]+)', line))
        records.append(pairs)
    assert records, 'missing touch-receipt diagnostic records'
    last_event = last_receipt = last_time = published_event = 0
    phases, sources = set(), set()
    pending_phases = []
    previous_publish_end = 0
    for r in records:
        for key in ('stage', 'eventseq', 'mono_ns', 'buttons', 'lx', 'ly', 'rx', 'ry', 'mode', 'source', 'phase'):
            assert key in r, f'missing {key}: {r}'
        assert r['source'] in SOURCES, f'unknown source={r["source"]}'
        assert r['phase'] in TOUCH_PHASES | {'none'}, f'unknown phase={r["phase"]}'
        event = int(r['eventseq'])
        mono = int(r['mono_ns'])
        assert mono >= last_time and mono > 0, 'nonmonotonic clock'
        assert mono >= previous_publish_end, 'record precedes previous publication end'
        last_time = mono
        if r['stage'] == 'event':
            assert event == last_event + 1, f'nonphysical or lost eventseq {last_event}->{event}'
            assert r['source'] == 'uitouch'
            assert r['phase'] in TOUCH_PHASES, 'event cannot use none phase'
            for key in ('slot', 'x', 'y', 'touch', 'touch_timestamp_s'):
                assert key in r
            assert math.isfinite(float(r['touch_timestamp_s'])) and float(r['touch_timestamp_s']) >= 0
            phases.add(r['phase'])
            pending_phases.append(r['phase'])
            last_event = event
        elif r['stage'] == 'output':
            assert event == last_event, f'program push changed eventseq {last_event}->{event}'
            for key in ('event_first_seq', 'event_last_seq'):
                assert key in r, f'missing publication {key}'
            first, last = int(r['event_first_seq']), int(r['event_last_seq'])
            assert last == event, 'publication group tail must match current eventseq'
            assert first == (published_event + 1 if event > published_event else 0), 'publication group must contain all and only newly received events'
            if first:
                assert r['source'] == 'uitouch', 'program source must not claim new physical events'
            assert all(phase == r['phase'] for phase in pending_phases), 'event group phase differs from output phase'
            pending_phases.clear()
            published_event = event
            receipt = int(r['receiptseq'])
            assert receipt == last_receipt + 1
            last_receipt = receipt
            previous_publish_end = int(r['publish_end_ns'])
            assert previous_publish_end >= mono, 'publication end precedes start'
            if r['source'] == 'portal_sequence':
                for key in ('portal_request','portal_binding','portal_scene','portal_phase','portal_pending','modifier_publish_end_ns','feedback_group_start_ns','feedback_captured_ns','feedback_secondary','feedback_seen','feedback_identity_scope'):
                    assert key in r, f'missing portal_sequence {key}'
                assert all(int(r[key]) > 0 for key in ('portal_request','portal_binding','portal_scene'))
                assert int(r['portal_phase']) == 2 and int(r['portal_pending']) == 0
                assert 0 < int(r['modifier_publish_end_ns']) < mono, 'ACK must follow actual modifier publication'
                assert int(r['feedback_secondary']) == 1 and int(r['feedback_seen']) == 63
                assert int(r['feedback_identity_scope']) in {1, 2}, 'unconfirmed HUD identity cannot ACK'
                assert int(r['modifier_publish_end_ns']) < int(r['feedback_group_start_ns']) <= int(r['feedback_captured_ns']) <= mono
                mask = int(r['buttons'], 0)
                assert mask & 256 and mask & (1|2|4|8|128|512), 'ACK must publish modifier plus facet'
            sources.add(r['source'])
            assert (r['phase'] != 'none') == (r['source'] == 'uitouch')
        else:
            raise AssertionError(r)
    assert published_event == last_event, 'trailing physical events were not published'
    print(f'LOG PASS: records={len(records)} events={last_event} outputs={last_receipt} phases={",".join(sorted(phases))} sources={",".join(sorted(sources))}')
    return records, phases, sources


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--source')
    p.add_argument('--log')
    p.add_argument('--probe', action='store_true', help='要求确定性 FakeTouch 探针的13事件和全部取消来源')
    args = p.parse_args()
    try:
        if args.source:
            check_source(args.source)
        if args.log:
            records, phases, sources = check_log(args.log)
            if args.probe:
                assert phases == {'began', 'moved', 'ended', 'cancelled'}
                assert sources == {'uitouch', 'layout', 'navigation', 'focus_cancel', 'screenshot'}
                events = [r for r in records if r['stage'] == 'event']
                assert len(events) == 13, f'expected 13 independent probe events, got {len(events)}'
                assert [r['phase'] for r in events[:9]] == ['began','moved','moved','ended','began','cancelled','began','moved','ended']
                assert int(events[1]['lx']) == 32767, '60pt displacement must produce max left x'
                assert int(events[2]['slot']) == 0 and int(events[2]['lx']) == 0, 'navigation cancellation must keep physical slot but zero controls'
                assert int(events[7]['slot']) == -1 and float(events[7]['x']) == 147.0 and int(events[7]['lx']) == 0, 'focus cancellation must still record unowned physical point'
                assert int(events[8]['slot']) == -1 and events[8]['phase'] == 'ended'
                assert all(int(events[i]['buttons'], 0) == 0 for i in range(9))
                outputs = [r for r in records if r['stage'] == 'output']
                expected_sources = ['navigation', 'navigation', 'uitouch', 'uitouch', 'navigation',
                    'uitouch', 'uitouch', 'navigation', 'uitouch', 'uitouch', 'uitouch',
                    'focus_cancel', 'uitouch', 'uitouch', 'layout', 'screenshot', 'uitouch', 'uitouch']
                assert [r['source'] for r in outputs] == expected_sources, 'probe publication source order mismatch'
                groups = [(int(r['event_first_seq']), int(r['event_last_seq'])) for r in outputs if int(r['event_first_seq'])]
                assert groups == [(i, i) for i in range(1, 10)] + [(10, 11), (12, 13)], 'two-touch batches must publish both events'
                assert {int(r['slot']) for r in events[9:11]} == {0, 1}
                assert {int(r['slot']) for r in events[11:13]} == {0, 1}
                assert all(float(r['touch_timestamp_s']) == 42.125 for r in events), 'FakeTouch timestamp must stay unconverted'
                print('PROBE PASS: 13/13 events; 4/4 phases; 5/5 sources; navigation slot kept; focus point kept; cancel axis reset; 11/11 event groups; raw timestamp=42.125')
    except AssertionError as e:
        print(f'FAIL: {e}')
        raise SystemExit(1)
