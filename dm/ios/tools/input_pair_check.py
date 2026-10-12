#!/usr/bin/env python3
"""冻结UI/HID收据的独立只读配对；协议通过不等于物理触摸通过。 | Independent read-only pairing of frozen UI/HID receipts; the protocol passing does not mean physical touch passes."""
import argparse
import json
import math
import re
from pathlib import Path

STATE = ("buttons", "lx", "ly", "rx", "ry")
PHASES = {"began", "moved", "ended", "cancelled"}
SOURCES = {"uitouch", "layout", "navigation", "focus_cancel", "screenshot", "bindings", "portal_sequence"}
UI_INTS = {"eventseq", "receiptseq", "event_first_seq", "event_last_seq",
           "mono_ns", "publish_end_ns", "slot", *STATE}
HID_INTS = {"read_start_ns", "time_ns", "sampling", "id", "style",
            "attributes", "script_mask", *STATE}


class TraceError(ValueError):
    pass


def integer(value):
    if isinstance(value, bool):
        raise TraceError("boolean_not_integer")
    if isinstance(value, int):
        return value
    if isinstance(value, str) and re.fullmatch(r"-?(?:0[xX][0-9a-fA-F]+|[0-9]+)", value):
        try:
            return int(value, 16) if "x" in value.lower() else int(value, 10)
        except ValueError:
            pass
    raise TraceError("bad_integer")


def normalize(records, keys):
    out = []
    for src in records:
        row = dict(src)
        for key in keys & row.keys():
            row[key] = integer(row[key])
        out.append(row)
    return out


def require(row, fields):
    missing = set(fields) - row.keys()
    if missing:
        raise TraceError("missing_fields:" + ",".join(sorted(missing)))


def validate_state(row):
    # 校验实际Npad ABI类型，而不是新增±32767生产阈值。
    # Validates the actual Npad ABI type, rather than adding a new ±32767 production threshold.
    if not 0 <= row["buttons"] < 2**64:
        raise TraceError("unsigned_range")
    if any(not -(2**31) <= row[k] < 2**31 for k in ("lx", "ly", "rx", "ry")):
        raise TraceError("int32_range")


PORTAL_FIELDS = {"portal_request", "portal_binding", "portal_scene", "portal_phase",
                 "portal_pending", "modifier_publish_end_ns", "feedback_group_start_ns",
                 "feedback_captured_ns", "feedback_secondary", "feedback_seen",
                 "feedback_identity_scope"}
PORTAL_UNSIGNED = PORTAL_FIELDS - {"portal_phase", "portal_pending", "feedback_secondary",
                                   "feedback_seen", "feedback_identity_scope"}


def validate_portal(row, active, request):
    if row["source"] == "portal_sequence" or PORTAL_FIELDS & row.keys():
        require(row, PORTAL_FIELDS)
    else:
        if request:
            raise TraceError("portal_fields_missing_during_request")
        return None
    if any(not 0 <= row[k] < 2**64 for k in PORTAL_UNSIGNED):
        raise TraceError("portal_unsigned_range")
    if (row["portal_phase"] not in {0, 1, 2} or row["portal_pending"] not in {0, 1} or
            row["feedback_secondary"] not in {0, 1} or not 0 <= row["feedback_seen"] <= 63 or
            row["feedback_identity_scope"] not in {0, 1, 2}):
        raise TraceError("portal_field_range")
    if row["source"] == "portal_sequence":
        if (not request or request["acked"] or not active or row["mode"] != "game" or
                row["portal_phase"] != 2 or row["portal_pending"] != 0):
            raise TraceError("portal_request_not_pending")
        modifier = request["modifier"]
        if not any(t in active and active[t] >= 0 for t in request["touches"]):
            raise TraceError("portal_touch_not_active")
        if any(row[k] != modifier[k] for k in ("portal_request", "portal_binding", "portal_scene",
                                               "modifier_publish_end_ns")):
            raise TraceError("portal_request_identity")
        if row["feedback_secondary"] != 1 or row["feedback_seen"] != 63 or row["feedback_identity_scope"] not in {1, 2}:
            raise TraceError("portal_feedback_identity")
        if not (modifier["publish_end_ns"] < row["feedback_group_start_ns"] <=
                row["feedback_captured_ns"] < row["mono_ns"] and
                row["mono_ns"] - row["feedback_captured_ns"] <= 250_000_000):
            raise TraceError("portal_feedback_time")
        # 最小两阶段原SDK位序；仅承认原facet A/B/X/Y/R/ZR的一个新增边沿。
        # Minimal two-phase original SDK bit order; only one newly added edge of the original facets A/B/X/Y/R/ZR is accepted.
        edge = row["buttons"] & ~modifier["buttons"]
        if not row["buttons"] & 256 or edge not in {1, 2, 4, 8, 128, 512}:
            raise TraceError("portal_action_edge")
        row["_portal_modifier_receipt"] = modifier["receiptseq"]
        request["acked"] = True
        return request
    if row["portal_phase"] == 1:
        if (row["portal_pending"] != 1 or row["mode"] != "game" or not active or
                not row["buttons"] & 256 or not all(row[k] for k in ("portal_request", "portal_binding", "portal_scene"))):
            raise TraceError("portal_modifier_publication")
        same = request and all(row[k] == request["modifier"][k] for k in
                               ("portal_request", "portal_binding", "portal_scene"))
        if same:
            if request["acked"] or row["modifier_publish_end_ns"] != request["modifier"]["publish_end_ns"]:
                raise TraceError("portal_modifier_publication")
            return request
        if (row["source"] != "uitouch" or row["phase"] != "began" or
                row["modifier_publish_end_ns"] != row["publish_end_ns"]):
            raise TraceError("portal_modifier_publication")
        return {"modifier": row, "acked": False,
                "touches": {e["touch"] for e in row["_touch_group"] if e["phase"] == "began" and e["slot"] >= 0}}
    if row["portal_pending"]:
        raise TraceError("portal_request_not_pending")
    if row["portal_phase"] == 0:
        return None
    # normal Portal无需ZL/ACK；已ACK请求的普通持有发布保留其关联。
    # normal Portal needs no ZL/ACK; the ordinary hold publish of an already ACKed request keeps its association.
    if request and all(row[k] == request["modifier"][k] for k in
                       ("portal_request", "portal_binding", "portal_scene")):
        if not request["acked"]:
            raise TraceError("portal_action_without_ack")
        return request
    return None


def validate_ui(records):
    records = normalize(records, UI_INTS | PORTAL_FIELDS)
    events = []
    outputs = []
    active = {}
    pending = []
    event_no = receipt_no = published = last_time = end_time = 0
    lifecycle_reasons = set()
    portal_request = None
    portal_epoch = 0
    for row in records:
        require(row, ("stage", "eventseq", "mono_ns", "source", "phase", "mode", *STATE))
        validate_state(row)
        if row["source"] not in SOURCES or row["mode"] not in {"game", "navigation"}:
            raise TraceError("bad_source_or_mode")
        if row["mono_ns"] <= 0 or row["mono_ns"] < max(last_time, end_time):
            raise TraceError("ui_time_regression")
        last_time = row["mono_ns"]
        if row["stage"] == "event":
            require(row, ("slot", "touch", "x", "y", "touch_timestamp_s"))
            if row["source"] != "uitouch" or row["phase"] not in PHASES:
                raise TraceError("bad_event_source_phase")
            if row["eventseq"] != event_no + 1:
                raise TraceError("event_sequence_gap")
            if not -1 <= row["slot"] < 16:
                raise TraceError("bad_slot")
            for key in ("x", "y", "touch_timestamp_s"):
                if not math.isfinite(float(row[key])):
                    raise TraceError("nonfinite_touch")
            if float(row["touch_timestamp_s"]) < 0:
                raise TraceError("negative_touch_timestamp")
            touch = row["touch"]
            if row["phase"] == "began":
                if touch in active:
                    lifecycle_reasons.add("duplicate_begin")
                if row["slot"] >= 0 and row["slot"] in active.values():
                    raise TraceError("slot_conflict")
                active[touch] = row["slot"]
            else:
                if touch not in active:
                    lifecycle_reasons.add("lifecycle_orphan")
                elif active[touch] != row["slot"]:
                    raise TraceError("slot_transition")
                if row["phase"] in {"ended", "cancelled"}:
                    active.pop(touch, None)
            event_no += 1
            pending.append(row)
            events.append(row)
        elif row["stage"] == "output":
            require(row, ("receiptseq", "event_first_seq", "event_last_seq", "publish_end_ns"))
            if row["receiptseq"] != receipt_no + 1 or row["eventseq"] != event_no:
                raise TraceError("output_sequence_gap")
            first = published + 1 if event_no > published else 0
            if row["event_first_seq"] != first or row["event_last_seq"] != event_no:
                raise TraceError("event_group_not_closed")
            if pending and (row["source"] != "uitouch" or
                            any(e["phase"] != row["phase"] for e in pending)):
                raise TraceError("event_group_source_phase")
            if (row["source"] == "uitouch") != (row["phase"] in PHASES):
                raise TraceError("bad_output_phase")
            if row["publish_end_ns"] < row["mono_ns"]:
                raise TraceError("reversed_publication")
            row["_touch_group"] = list(pending)
            row["_release"] = bool(pending) and not active and row["phase"] in {"ended", "cancelled"}
            row["_blank_claim"] = any(e.get("blank") is True for e in pending)
            portal_request = validate_portal(row, active, portal_request)
            if "portal_request" in row:
                if row["portal_request"] < portal_epoch:
                    raise TraceError("portal_request_regression")
                portal_epoch = row["portal_request"]
            outputs.append(row)
            if row["source"] in {"layout", "focus_cancel"}:
                # 控件取消/清槽不代表物理触点已结束；后续无归属点slot=-1仍按touch身份闭合。
                # A widget cancel / slot clear does not mean the physical touch point has ended; a subsequent unowned point with slot=-1 is still closed under the touch identity.
                active = {touch: -1 for touch in active}
            pending.clear()
            receipt_no += 1
            published = event_no
            end_time = row["publish_end_ns"]
        else:
            raise TraceError("bad_ui_stage")
    if pending:
        lifecycle_reasons.add("unpublished_events")
    if active:
        lifecycle_reasons.add("lifecycle_unclosed")
    if not records:
        raise TraceError("empty_ui_trace")
    return events, outputs, lifecycle_reasons


def validate_hid(records):
    records = normalize(records, HID_INTS)
    rows = []
    seen = set()
    reasons = set()
    for row in records:
        if row.get("event") == "limit_reached":
            reasons.add("limit_reached")
            continue
        if row.get("event") != "npad_return":
            raise TraceError("bad_hid_event")
        require(row, HID_INTS)
        validate_state(row)
        if row["read_start_ns"] < 0 or row["time_ns"] < row["read_start_ns"]:
            raise TraceError("reversed_read")
        if not 0 < row["sampling"] < 2**63:
            raise TraceError("bad_sampling")
        if any(not 0 <= row[k] < 2**32 for k in ("id", "style", "attributes")):
            raise TraceError("unsigned_range")
        if any(not 0 <= row[k] < 2**64 for k in ("buttons", "script_mask", "read_start_ns", "time_ns")):
            raise TraceError("unsigned_range")
        identity = (row["id"], row["style"], row["sampling"])
        if identity in seen:
            reasons.add("sampling_not_forward")
        seen.add(identity)
        rows.append(row)
    rows.sort(key=lambda r: (r["time_ns"], r["read_start_ns"]))
    last = {}
    for row in rows:
        key = (row["id"], row["style"])
        if key in last and row["sampling"] <= last[key]:
            reasons.add("sampling_not_forward")
        last[key] = row["sampling"]
    return rows, reasons


def connected(row):
    return bool(row["attributes"] & 1) and (
        (row["id"] == 0 and row["style"] == 1) or
        (row["id"] == 32 and row["style"] == 2))


def analyze(ui, hid, domain="synthetic"):
    result = {"domain": domain, "physical_pass_claimed": False,
              "release_received": False, "blank_activity_received": False,
              "combo_received": False, "portal_combo_pairs": 0}
    try:
        events, outputs, reasons = validate_ui(ui)
        result.update(ui_events=len(events), ui_outputs=len(outputs))
        if domain == "device" and any(o["source"] == "portal_sequence" and o["feedback_identity_scope"] != 1 for o in outputs):
            raise TraceError("synthetic_identity_on_device")
        if hid is None:
            result.update(status="pair_not_executed", reasons=sorted(reasons | {"missing_hid_stream"}),
                          ui_lifecycle_closed=not reasons, pairs=[])
            return result
        reads, h_reasons = validate_hid(hid)
        reasons.update(h_reasons)
        pairs = []
        touch_pairs = 0
        matched_reads = {}
        for i, output in enumerate(outputs):
            upper = outputs[i + 1]["mono_ns"] if i + 1 < len(outputs) else None
            lo = output["publish_end_ns"]
            stable = [r for r in reads if r["read_start_ns"] > lo and
                      (upper is None or r["time_ns"] < upper)]
            overlaps = [r for r in reads if
                        r["time_ns"] >= output["mono_ns"] and
                        (upper is None or r["read_start_ns"] < upper) and
                        any(r["read_start_ns"] <= o["publish_end_ns"] and
                            r["time_ns"] >= o["mono_ns"] for o in outputs)]
            candidates = [r for r in stable if connected(r) and r["script_mask"] == 0]
            matched = [r for r in candidates if all(r[k] == output[k] for k in STATE)]
            is_touch = output["source"] == "uitouch" and bool(output["_touch_group"])
            pair = {"receiptseq": output["receiptseq"], "source": output["source"],
                    "event_group": [output["event_first_seq"], output["event_last_seq"]],
                    "matched_sampling": [r["sampling"] for r in matched],
                    "status": "matched" if matched else "not_sampled"}
            is_combo = output["source"] == "portal_sequence"
            if (is_touch or is_combo) and not matched:
                reasons.add("missing_combo_receipt" if is_combo else "missing_receipt")
                if overlaps:
                    reasons.add("concurrent_undetermined")
                    pair["status"] = "concurrent_undetermined"
                elif stable and not any(connected(r) for r in stable):
                    reasons.add("invalid_controller")
                    pair["status"] = "invalid_controller"
                elif any(connected(r) and r["script_mask"] for r in stable):
                    reasons.add("script_confounded")
                    pair["status"] = "script_confounded"
                elif candidates:
                    reasons.add("returned_state_mismatch")
                    pair["status"] = "state_mismatch"
            matched_reads[output["receiptseq"]] = matched
            if is_combo:
                modifier_reads = matched_reads.get(output["_portal_modifier_receipt"], [])
                before_feedback = [r for r in modifier_reads
                                   if r["time_ns"] < output["feedback_group_start_ns"]]
                modifier_received = bool(before_feedback)
                pair["modifier_before_feedback_sampling"] = [r["sampling"] for r in before_feedback]
                pair["modifier_receiptseq"] = output["_portal_modifier_receipt"]
                pair["combo_received"] = bool(matched) and modifier_received
                if not modifier_received:
                    reasons.add("modifier_not_before_feedback" if modifier_reads else "missing_modifier_receipt")
                if pair["combo_received"]:
                    result["combo_received"] = True
                    result["portal_combo_pairs"] += 1
            neutral = all(output[k] == 0 for k in STATE)
            if output["_release"] and not neutral:
                reasons.add("release_not_neutral")
            if output["_blank_claim"] and output["buttons"] & 1:
                reasons.add("blank_unwanted_A")
            if matched and is_touch:
                touch_pairs += 1
                if output["_release"] and neutral:
                    result["release_received"] = True
                if output["_blank_claim"] and not (output["buttons"] & 1):
                    result["blank_activity_received"] = True
            # 程序来源可匹配值，但不能算新真实触摸。
            # A program source may match values, but must not count as a new real touch.
            if not is_touch:
                pair["attribution"] = "output_only_not_new_touch"
            pairs.append(pair)
        if not touch_pairs:
            reasons.add("no_touch_pairs")
        result.update(hid_rows=len(reads), touch_output_pairs=touch_pairs,
                      pairs=pairs, reasons=sorted(reasons),
                      status="incomplete" if reasons else "protocol_pass",
                      unknowns=["no physical case/game result established",
                                "blank-area identity requires independent gesture plan",
                                "feedback identity/group are UI declarations; native caller proof needs frozen source and original capture"])
    except (TraceError, ValueError, TypeError, KeyError) as error:
        result.update(status="invalid_trace", reasons=[str(error)], pairs=[])
    return result


def load_ui(path):
    records = []
    for line in Path(path).read_text(errors="replace").splitlines():
        if "[touch-receipt]" in line:
            records.append(dict(re.findall(r"(\w+)=([^\s]+)", line)))
    return records


def load_hid(path):
    return [json.loads(line) for line in Path(path).read_text().splitlines() if line.strip()]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ui-log", required=True)
    parser.add_argument("--hid-log")
    parser.add_argument("--domain", choices=["synthetic", "fake", "simulator", "device"], required=True)
    parser.add_argument("--out")
    parser.add_argument("--blank-events", default="",
                        help="由独立手势计划指定空白触点eventseq，逗号分隔；不从零输出猜空白区域")
    args = parser.parse_args()
    try:
        ui = load_ui(args.ui_log)
        blanks = {int(v) for v in args.blank_events.split(",") if v}
        observed = set()
        for row in ui:
            if row.get("stage") == "event" and int(row["eventseq"]) in blanks:
                row["blank"] = True
                observed.add(int(row["eventseq"]))
        if observed != blanks:
            raise TraceError("blank_plan_event_missing")
        data = analyze(ui, load_hid(args.hid_log) if args.hid_log else None, args.domain)
        data["blank_plan_events"] = sorted(blanks)
    except (OSError, ValueError) as error:
        data = {"status": "invalid_trace", "reasons": [str(error)],
                "domain": args.domain, "physical_pass_claimed": False}
    text = json.dumps(data, ensure_ascii=False, indent=2)
    if args.out:
        Path(args.out).write_text(text + "\n")
    print(text)
    return {"protocol_pass": 0, "pair_not_executed": 2}.get(data["status"], 1)


if __name__ == "__main__":
    raise SystemExit(main())
