"""触屏全链用例断言公共库。

面板开关按 uimsg.jsonl 每行的 ``tick``（宿主 DM_UIMSG 在局内写服务端当前拍号）对齐；
局外（无 tick）的消息归入此前最近的拍号，首个拍号之前归入拍 0。
Shared assertion library for touch full-chain cases.
Panel open/close is aligned by the ``tick`` of each line in uimsg.jsonl (in-run, the host's DM_UIMSG writes the server's current tick number);
out-of-run messages (no tick) are attributed to the most recent preceding tick number, and those before the first tick number to tick 0.
"""

import json
import re
import sys
from pathlib import Path


class AssertFail(AssertionError):
    pass


_FAILURES = []
_CHECKS = 0
_TICK_RE = re.compile(r"^(?:DM:Tick:)?(\d+)(?:#)?$")


def check(cond, msg):
    global _CHECKS
    _CHECKS += 1
    if not cond:
        _FAILURES.append(str(msg))
    return bool(cond)


def _read_jsonl(path):
    try:
        with path.open(encoding="utf-8") as stream:
            return [json.loads(line) for line in stream if line.strip()]
    except (OSError, ValueError) as exc:
        raise AssertFail(f"无法读取 {path}: {exc}") from exc


def _required(directory, name):
    path = directory / name
    if not path.is_file():
        raise AssertFail(f"缺少必需文件：{path}")
    return path


def _tick_from_message(row):
    if isinstance(row.get("tick"), int):
        return row["tick"]
    if row.get("cat") == "DM" and row.get("act") == "Tick":
        match = _TICK_RE.match(str(row.get("arg", "")))
        if match:
            return int(match.group(1))
    if row.get("act") == "DM:Tick":
        match = _TICK_RE.match(str(row.get("arg", "")))
        if match:
            return int(match.group(1))
    return None


class Result:
    def __init__(self, directory, c2s, ticks, uimsg, state):
        self.directory = directory
        self._c2s = c2s
        self._ticks = ticks
        self._uimsg = uimsg
        self._state = state
        self._state_by_tick = {}
        for row in state:
            tick = row.get("tick")
            if isinstance(tick, int):
                self._state_by_tick[tick] = row
        self._panel_events = self._build_panel_events(uimsg)

    @staticmethod
    def _build_panel_events(rows):
        current_tick = 0
        events = []
        for row in rows:
            if isinstance(row.get("tick"), int):      # 宿主 DM_UIMSG 局内每行带服务端拍号 | In-run, each line of the host's DM_UIMSG carries the server tick number
                current_tick = row["tick"]
            else:
                marker_tick = _tick_from_message(row)
                if marker_tick is not None:
                    current_tick = marker_tick
                    continue
            action = row.get("act")
            panel = row.get("arg")
            if not panel or action not in {
                "OpenPanel", "OpenExclusivePanel", "UnloadPanel", "ClosePanel"
            }:
                continue
            events.append((current_tick, action, panel))
        return events

    def _state_at(self, tick):
        rows = [item for item in self._state_by_tick if item <= tick]
        if not rows:
            return None
        return self._state_by_tick[max(rows)]

    def items_at(self, tick):
        row = self._state_at(tick)
        if row is None:
            return {}
        return {item["id"]: dict(item) for item in row.get("items", [])}

    def cursor_at(self, tick):
        row = self._state_at(tick)
        return None if row is None else row.get("cursor")

    def players_at(self, tick):
        row = self._state_at(tick)
        return None if row is None else row.get("players")

    def panels_at(self, tick):
        panels = set()
        for event_tick, action, panel in self._panel_events:
            if event_tick > tick:
                break
            if action == "OpenExclusivePanel":
                panels.clear()
                panels.add(panel)
            elif action == "OpenPanel":
                panels.add(panel)
            else:
                panels.discard(panel)
        return panels

    def c2s_ops(self, t0=None, t1=None):
        result = []
        for row in self._c2s:
            if row.get("q") == "sys":
                continue
            tick = row.get("tick")
            if not isinstance(tick, int) or (t0 is not None and tick < t0) or (
                t1 is not None and tick > t1
            ):
                continue
            packet = str(row.get("pkt", ""))
            if len(packet) < 2:
                raise AssertFail(f"命令包缺少 opcode：tick={tick} pkt={packet!r}")
            try:
                opcode = int(packet[:2], 16)
            except ValueError as exc:
                raise AssertFail(f"命令包不是十六进制：tick={tick} pkt={packet!r}") from exc
            result.append((tick, opcode, packet))
        return result

    def state_rows(self):
        return list(self._state)

    def tick_units(self):
        return [row["u"] for row in self._ticks if isinstance(row.get("u"), dict)]


def load(directory):
    directory = Path(directory)
    paths = {name: _required(directory, name) for name in (
        "c2s.jsonl", "ticks.jsonl", "uimsg.jsonl", "state.jsonl"
    )}
    return Result(
        directory,
        _read_jsonl(paths["c2s.jsonl"]),
        _read_jsonl(paths["ticks.jsonl"]),
        _read_jsonl(paths["uimsg.jsonl"]),
        _read_jsonl(paths["state.jsonl"]),
    )


def main(argv=None):
    del argv
    if _FAILURES:
        print(f"红 {len(_FAILURES)} 条")
        for failure in _FAILURES:
            print(f"- {failure}")
        return 1
    print(f"绿 {_CHECKS} 条")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
