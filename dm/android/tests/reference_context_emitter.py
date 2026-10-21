from __future__ import annotations

import ast
import itertools
from pathlib import Path
import re
import struct

import capstone
from capstone import arm64
import unicorn
from unicorn import arm64_const as registers

CODE = 0x100000
KEYS = 0x110000
CONTEXT = 0x120000
ENTRY = 0x130000
RETURN = 0x140004
STACK = 0x200000
SP = STACK + 0x8000
SLOTS = ("K18", "KTLS", "KTLR", "KGATE", "KRES0", "KRES1", "KASPECT", "KASPECTUI")


def functions(target: str) -> dict:
    source = Path(__file__).resolve().parents[2] / "tools" / "patch_nso.py"
    nodes = [node for node in ast.parse(source.read_text()).body if isinstance(node, ast.FunctionDef)]
    decoder = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
    decoder.detail = True
    namespace = {
        "TARGET": target, "PATCH_OFF": CODE, "KEY_PAGE_OFF": KEYS,
        "KEY_INDEX": dict(zip(SLOTS, range(8))), "patch": bytearray(),
        "fix": [], "gates": [], "stats": {}, "md": decoder,
        "struct": struct, "re": re, "combinations": itertools.combinations,
    }
    namespace.update(vars(arm64))
    namespace.update({key: value for key, value in vars(capstone).items() if key.startswith("CS_")})
    exec(compile(ast.Module(body=nodes, type_ignores=[]), str(source), "exec"), namespace)
    return namespace


def execute(target: str, operation: str, number: int) -> None:
    ns = functions(target)
    word = (0xD53BD040 if operation == "read" else 0xD51BD040) | number
    instruction = ns["dis"](word, RETURN - 4)
    if operation == "read":
        ns["site_sysreg_read"](instruction, "KTLS")
    else:
        ns["site_tpidr_write"](instruction)
    machine = unicorn.Uc(unicorn.UC_ARCH_ARM64, unicorn.UC_MODE_ARM)
    for address, size in [(CODE, 0x10000), (KEYS, 0x1000), (CONTEXT, 0x1000),
                          (ENTRY, 0x1000), (RETURN & ~0xfff, 0x1000), (STACK, 0x10000)]:
        machine.mem_map(address, size)
    machine.mem_write(CODE, bytes(ns["patch"]))
    machine.mem_write(KEYS, struct.pack("<9Q", *range(0, 64, 8), ENTRY))
    values = [0xbeef18, 0xabc001, 0xabc002, 0xabc003, 4, 5, 6, 7]
    machine.mem_write(CONTEXT, struct.pack("<8Q", *values))
    original = [0xa5000000 + index * 16 for index in range(31)]
    for index, value in enumerate(original):
        machine.reg_write(getattr(registers, f"UC_ARM64_REG_X{index}"), value)
    machine.reg_write(registers.UC_ARM64_REG_SP, SP)
    machine.reg_write(registers.UC_ARM64_REG_NZCV, 0xa0000000)
    machine.reg_write(registers.UC_ARM64_REG_TPIDRRO_EL0, CONTEXT if target == "apple" else 0xdead0000)
    machine.mem_write(SP - 256, bytes([0x5a]) * 80)
    calls = []

    def context_entry(uc, address, size, data):
        if address == ENTRY:
            calls.append(address)
            uc.reg_write(registers.UC_ARM64_REG_X16, CONTEXT)
            uc.reg_write(registers.UC_ARM64_REG_PC, uc.reg_read(registers.UC_ARM64_REG_X30))

    machine.hook_add(unicorn.UC_HOOK_CODE, context_entry)
    machine.emu_start(CODE, RETURN, count=300)
    assert machine.reg_read(registers.UC_ARM64_REG_PC) == RETURN
    assert machine.reg_read(registers.UC_ARM64_REG_SP) == SP
    assert machine.reg_read(registers.UC_ARM64_REG_NZCV) == 0xa0000000
    assert bytes(machine.mem_read(SP - 256, 80)) == bytes([0x5a]) * 80
    after = list(struct.unpack("<8Q", machine.mem_read(CONTEXT, 64)))
    expected = original.copy()
    expected_slots = values.copy()
    if operation == "read":
        if target == "android" and number == 18:
            expected_slots[0] = values[1]
        else:
            expected[number] = values[1]
    else:
        expected_slots[1] = values[0] if target == "android" and number == 18 else original[number]
    actual = [machine.reg_read(getattr(registers, f"UC_ARM64_REG_X{i}")) for i in range(31)]
    assert actual == expected, (target, operation, number, actual, expected)
    assert after == expected_slots, (target, operation, number, after, expected_slots)
    assert bool(calls) == (target == "android")


def main() -> None:
    count = 0
    for target in ("apple", "android"):
        for operation in ("read", "write"):
            for number in range(31):
                execute(target, operation, number)
                count += 1
    print(f"PASS context emitter: {count} owned ARM64 executions; registers, flags, stack, slots")


if __name__ == "__main__":
    main()
