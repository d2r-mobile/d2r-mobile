# UI Message Stream Format

中文：[ui_message_stream.zh-CN.md](ui_message_stream.zh-CN.md)

Every change in the game's UI (opening a panel, closing a panel, switching pages, moving the cursor…) is announced by the game itself with a UI message. Recording the UI messages of a run in order into a file makes it possible to compare whether the UI flow is the same on both sides: one side is the **ground truth** (reference data obtained by running the original game in a patched Ryujinx emulator), the other is the **host** (`dmhost` from `dm/host`, which executes the rewritten game program on a Mac). This page defines the format of that file and the comparison rules.

## Capture point

- All of the game's UI messages are dispatched through virtual function `+0x28` of the UI bus object `*(main + 0x2f05458)`; in `main` of 3.3.94012, the implementing function is `main + 0xb1dc60` (the vtable read at run time is authoritative; it is not hard-coded).
- At entry: `x0` = the bus object, `x1` → the message header `{+0x00 u64 category hash, +0x08 u64 action hash, +0x10 u64 payload pointer}`.
- The hash is FNV-1a 64 (offset basis `0xcbf29ce484222325`, multiplier `0x100000001b3`), computed byte by byte over the original string. Example: `PanelManager` = `0x7127a406e0b6452c`.
- The same virtual function occasionally has non-message calls (`x1` is not a readable pointer): these are not output.

## One line of JSON per message

UTF-8, terminated by `\n`, with a fixed field order:

```
{"seq":N,"cat_h":"0x7127a406e0b6452c","act_h":"0x...","arg":"MainMenuPanel","cat":"PanelManager","act":"OpenExclusivePanel"}
```

| Field | Required | Meaning |
|---|---|---|
| seq | Yes | Index within this run (starting from 0, counting only messages that are output) |
| cat_h / act_h | Yes | Category / action hash, `0x` + 16 lowercase hex digits. **Comparison uses only these two and arg** |
| arg | Yes | The C string the payload pointer points to (≤ 64 bytes, truncated at 0; must consist entirely of printable ASCII or UTF-8 high bytes), otherwise `null` |
| cat / act | No | Names, for human reading only: looked up in reverse by FNV-1a over all printable strings of ≥ 3 bytes in the read-only region `[0x1b79000, 0x2056000)` of `main`; omitted if not found |

- Time, frame number, thread number and pointer values are not output: these are not comparable between the two sides.
- File location: on the ground-truth side it is `<run dir>/uimsg.jsonl` (written by the emulator driver scripts; these are not shipped with the repository, so the checks that depend on ground truth cannot be run with this repository alone); on the host side it is given by the environment variable `DM_UIMSG=<path>`, and by default nothing is recorded.

## Comparison rules

Implemented by `dm/verify/uicmp.py`: `uicmp.py <ground-truth uimsg.jsonl> <host uimsg.jsonl> [--until <arg>]`, exit code 0 = identical.

1. Each side's sequence is taken as `(cat_h, act_h, arg)`.
2. Messages in the exemption table (see below) are removed first, then **adjacent, fully identical messages are folded into one**: only whether a message appears is compared, not how many times.
3. If the folded sequences are identical entry by entry, the two sides match; otherwise the position of the first difference is reported, together with 3 entries before and after that position on each side.
4. The coverage (how much this comparison actually covered) is reported as well: the entry counts before / after folding, and the number of distinct categories.

`--until <arg>`: both sides are truncated at the first occurrence of that payload (inclusive), so only the same segment of the flow is compared.

## Exemption table

Exemptions are registered in `EXEMPT` in `uicmp.py`; for every entry added, the reason must be stated here.

| Action | Payload | Reason |
|---|---|---|
| OpenPanel / ClosePanel | SaveIndicatorPanel | A timing race. `0x2442ac` sets the "saving" flag to 1 and then clears it to 0, less than 1 millisecond apart; only if a frame happens to fall inside this window does `0xb14e00` start showing the save indicator, after which `0xad42d4` keeps it shown for a minimum duration by wall clock, resending OpenPanel every frame during that time. Whether it appears depends on frame rate and luck, and is not comparable between the two sides. `LoadPanel SaveIndicatorPanel` is not exempt and is compared as usual |

Run requirement: save copies must always be writable. A read-only copy would make the save-writing step not happen, deviating from a real device.

## Cursor payload (used by the driver, not part of the comparison)

`+4` / `+8` at the payload pointer of `InputMessage:MouseMove` are two `u32` cursor coordinates. The driver script reads them, and in a ground-truth run's `events.jsonl` this UI event gets an extra `cursor: [x, y]`; `uimsg.jsonl` still has only the six fields above, and the comparison definition is unchanged.

A driver-script plan (an action sequence that a ground-truth run advances by game data) can use the cursor as a confirmation condition:

- `press <key> cursor:x:y:r released`: after the key is sent, a new cursor message must be seen landing within radius `r` of the target, and the last `ControllerActionBegin/End` must be End, before moving on to the next step.
- `assert cursor:x:y:r`: only checks the current cursor position, without requiring further movement.

Cursor coordinates vary with the window size and the page you are on; coordinates measured on one page cannot be extrapolated to other pages.
