from pathlib import Path
import re
from typing import Final

TYPES: Final = {"void": "V", "boolean": "Z", "int": "I", "long": "J", "float": "F", "String": "Ljava/lang/String;"}


def descriptor(java_type: str) -> str:
    return "[" + descriptor(java_type[:-2]) if java_type.endswith("[]") else TYPES[java_type]


def declarations(java: str) -> set[tuple[str, str, str]]:
    methods: set[tuple[str, str, str]] = set()
    for result, name, arguments in re.findall(r"\bnative\s+(\w+(?:\[\])?)\s+(\w+)\(([^)]*)\)\s*;", java):
        parameters = "".join(descriptor(arg.strip().split()[0]) for arg in arguments.split(",") if arg.strip())
        methods.add((name, "(" + parameters + ")" + descriptor(result), "Java_org_d2r_android_port_D2RActivity_" + name))
    assert methods, "no Activity native declarations found"
    return methods


def registrations(native: str) -> set[tuple[str, str, str]]:
    return set(re.findall(r'\{"(\w+)","([^\"]+)",\(void\*\)(\w+)\}', native))


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    java = (root / "java/org/d2r/android/port/D2RActivity.java").read_text()
    native = (root / "native/frontend_activity.c").read_text()
    expected = declarations(java)
    actual = registrations(native)
    assert expected == actual, f"Activity JNI mismatch: missing={expected - actual}, extra={actual - expected}"
    missing = native.replace('{"getWorldHudSnapshot","([F)I",(void*)Java_org_d2r_android_port_D2RActivity_getWorldHudSnapshot},', "")
    assert registrations(missing) != expected, "negative control failed to detect missing registration"
    wrong = native.replace('{"getWorldHudSnapshot","([F)I",', '{"getWorldHudSnapshot","([I)I",')
    assert registrations(wrong) != expected, "negative control failed to detect wrong descriptor"
    print(f"PASS: {len(expected)} Activity native declarations match explicit JNI registrations")


if __name__ == "__main__":
    main()
