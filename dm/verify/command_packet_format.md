# Client Command Packet Sequence Format

中文：[command_packet_format.zh-CN.md](command_packet_format.zh-CN.md)

Internally the game is split into two halves, client and server: the client turns the player's actions into "command packets" and hands them to the server to process. Recording the command packets of a run in order into a file has two uses: replaying them as-is on the **host** (`dmhost` from `dm/host`), so that the server plays out the same run as the **ground truth** (reference data obtained by running the original game in a patched Ryujinx emulator); or comparing whether the two sides emit the same command packets under the same key presses. Ground-truth recording and host replay share this format.

## Capture point

- The client command dispatch function `main + 0x6e5b40(G, unit, packet, length)`. At entry:
  `x0` = G (the game object, the same base address as in [tick_state_format.md](tick_state_format.md)), `x1` = the unit sending the packet (may be 0), `x2` → the packet bytes (packet number = first byte, must be < 0x67), `w3` = length.
- The handler table `main + 0x20a4190` (0x67 entries; an empty entry means an illegal packet number).
- **System-class messages** take another path: `main + 0x6310b0(buf, packet length)` (jump table `0x1daf73c`), buf = u32 client number + packet. The packet receive pump `0x5d3960` first drains the system-class queue each tick, then processes command packets.

  | Packet no. | Length | Meaning | Replay handling |
  |---|---|---|---|
  | 0x67 | | Join | Let through as usual at game entry (the host's own is used) |
  | 0x6c | | Character data chunk | Injected from the file |
  | 0x6d | 13 | Heartbeat: compares +0x6c4 and +0x6c0 of the client structure, and if they differ replies with a sync message and updates | Injected from the file |
  | 0x6f | 5 | Progress: the progress value reported by the client is written to +0x6c0 of the client structure (e.g. "new area finished loading" after a teleport) | Injected from the file |
  | 0x6a / 0x6b / 0x6e | | Discarded by default | — |

## One line of JSON per packet (in call order)

```
{"tick":57,"unit":1,"len":5,"pkt":"0300120034"}
```

| Field | Meaning |
|---|---|
| tick | `u32 [G+0x170]` at call time (the server's current tick number) |
| unit | `u32 [x1+0x8]` (unit id; null when x1 = 0) |
| len | `w3` |
| pkt | Lowercase hex of the first min(len, 512) bytes of the packet |
| q | Present only for system-class messages, value `"sys"`; in that case unit is always null, tick is the current tick number (`[G+0x170]` at the most recent tick entry), len = packet length, and pkt **does not include** the leading 4-byte client number |

Example of a system-class entry: `{"tick":201,"q":"sys","unit":null,"len":5,"pkt":"6f…"}`.

The host side records with `DM_C2S_LOG=<path>`, in the same format field by field.

## Two layers of criteria

1. **Server layer**: the host takes no controller input; at the tick whose number = tick, in file order, it hands each packet as-is to `0x6e5b40` (`DM_C2S_REPLAY=<file>`, which also turns off commands the client would issue on its own; for testing only), and the 6-slot state is required to match the ground truth tick by tick.
   - **System-class messages must be recorded from the ground truth and injected by the host tick by tick.** The tick number and value with which `0x6f` arrives determine at which tick the server inserts a sync message (in several hundred places the server compares +0x6c4 / +0x6c0 before sending a message and resends as needed), which depends on how fast the client is and is inherently different on the two sides, so it is timing-dependent input.
   - During replay, the host's own system-class messages are all discarded from the file's first tick on (the `0x67` join before game entry is let through as usual), and instead the original bytes recorded from the ground truth are injected at their original tick numbers; within one tick, system-class messages are injected first and command packets dispatched afterwards, matching the order of the packet receive pump.
   - After the file's last tick, the messages the host client issues on its own are let through again.
2. **Client layer**: under the same key script, the command packet sequences the two sides produce are compared **by content and order only** (tick is ignored), and in addition the UI message streams must match ([ui_message_stream.md](ui_message_stream.md)).

Comparison tool: `dm/verify/c2scmp.py <A c2s.jsonl> <B c2s.jsonl> [--ids]`. It reports two things: whether content and order match (the client-layer criterion), and whether packets with the same index land on the same tick number. `--ids` compares only the packet number sequence and folds consecutive identical packet numbers, for continuous actions such as the stick. Exit codes: 0 = content and landing ticks both match, 1 = content matches but landing ticks differ, 2 = content does not match, 3 = either side has 0 entries.

## Recording on the ground-truth side

Recording on the ground-truth side is done by the emulator driver scripts. These scripts are not shipped with the repository, so the checks that depend on ground truth cannot be run with this repository alone. Recording one run puts `c2s.jsonl` (this format) and `ticks.jsonl` (the tick state format) in the run directory. A run can be driven in two ways:

- **Action table**: each line is "tick number after game entry `<TAB>` input command".
- **Plan** (mutually exclusive with the action table): advances by game data. When walking toward an object or monster, the stick is pushed in a closed loop using the coordinates of the player and the target; every key press is confirmed by a UI message, a command packet, the missile count or a scene change.
- In the plan used for six-slot coverage at the server layer, the skill-select packet `0x3c` has its skill number rewritten in place on the ground-truth side, and the recorded `c2s.jsonl` contains the modified bytes — such a file is used only for server-layer replay, and the client-layer criterion cannot use a plan with such a rewrite.

## Two points to note when recording through GDB

The ground-truth side records by setting breakpoints at the two function entries above through the emulator's GDB interface. Both rules are implemented in `oracle/swgdb.py` (`wait_stop` switches threads automatically, `_stash` deduplicates, `step_over` waits for this thread to get past the breakpoint):

- Each time the emulator receives a breakpoint, it sets "which thread register reads refer to" to the thread that most recently hit a breakpoint. Before handling a stop packet that arrived earlier, you must first switch back to that packet's thread; otherwise you read another thread's PC and let this hit pass as an unrelated stop, causing a missed tick.
- A thread that is stopped at a breakpoint and not yet handled will stop again at the same breakpoint on every continue-all. Stashed stop packets must be deduplicated by thread, otherwise the same command packet is recorded twice.
