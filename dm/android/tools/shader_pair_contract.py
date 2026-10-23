import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "gfx/tools"))
from dmsh_spirv_contract import Violation, need, parse, string_at


def validate_pair(spirv, metadata):
    need(len(metadata) >= 28 and metadata[:4] == b"DMVK", "Invalid DMVK header")
    version, stage, descriptor_set, count, c1_size, attribute_count = struct.unpack_from("<6I", metadata, 4)
    need(version == 1 and count <= 256 and c1_size <= 65536 and attribute_count == 32,
         "Invalid DMVK version or capacity")
    need(len(metadata) == 28 + count * 40 + c1_size + 128, "Invalid DMVK length")
    models = {0: 0, 1: 4, 2: 3, 3: 1, 4: 2, 5: 5}
    need(stage in models and descriptor_set == (0 if stage == 5 else stage), "Invalid DMVK stage/set")
    bindings = {}
    for index in range(count):
        binding = struct.unpack_from("<10I", metadata, 28 + index * 40)
        need(binding[0] <= 4 and 1 <= binding[7] <= 128 and binding[1] not in bindings,
             "Invalid DMVK binding kind, count or duplicate")
        bindings[binding[1]] = binding
    attributes = struct.unpack_from("<32I", metadata, 28 + count * 40 + c1_size)
    instructions, bound, _ = parse(spirv)
    entries, types, decorations, constants, variables = [], {}, {}, {}, []
    for opcode, operands in instructions:
        if opcode == 15:
            need(len(operands) >= 3, "Truncated entry point")
            entries.append((operands[0], operands[1], string_at(operands, 2)))
        elif opcode in (21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 32):
            need(bool(operands) and 0 < operands[0] < bound and operands[0] not in types, "Invalid type ID")
            types[operands[0]] = (opcode, operands[1:])
        elif opcode == 43:
            need(len(operands) >= 3, "Truncated constant")
            constants[operands[1]] = operands[2:]
        elif opcode == 71:
            need(len(operands) >= 2, "Truncated decoration")
            key = (operands[0], operands[1])
            need(key not in decorations or decorations[key] == operands[2:], "Conflicting decoration")
            decorations[key] = operands[2:]
        elif opcode == 59:
            need(len(operands) >= 3 and 0 < operands[1] < bound, "Invalid variable")
            variables.append(operands[:3])
    need(len(entries) == 1 and entries[0][0] == models[stage] and entries[0][2] == "main"
         and 0 < entries[0][1] < bound, "SPIR-V entry/stage differs from DMVK")

    def type_at(identifier, opcode=None):
        need(identifier in types, "Missing SPIR-V type")
        kind, values = types[identifier]
        need(opcode is None or kind == opcode, "Unexpected SPIR-V type")
        return kind, values

    def decoration(identifier, kind):
        values = decorations.get((identifier, kind))
        need(values is not None and len(values) == 1, "Missing or invalid SPIR-V decoration")
        return values[0]

    seen, inputs = set(), {}
    for pointer, variable, storage in variables:
        _, pointee = type_at(pointer, 32)
        need(len(pointee) == 2 and pointee[0] == storage, "Invalid variable pointer storage")
        target = pointee[1]
        if stage == 0 and storage == 1 and (variable, 30) in decorations:
            location = decoration(variable, 30)
            kind, values = type_at(target)
            if kind == 23:
                need(len(values) == 2, "Invalid input vector")
                kind, values = type_at(values[0])
            need(kind in (21, 22) and len(values) == (2 if kind == 21 else 1), "Unsupported vertex input type")
            need(location < 32 and location not in inputs, "Invalid or duplicate vertex input location")
            inputs[location] = 1 if kind == 22 else 2 if values[1] else 3
        if storage not in (0, 2, 12):
            continue
        binding_number = decoration(variable, 33)
        actual_set = decoration(variable, 34)
        support = actual_set == 0 and binding_number == 0 and bindings.get(0, (None,))[0] == 0
        need((actual_set == descriptor_set or support) and (actual_set, binding_number) not in seen,
             "Wrong descriptor set or duplicate SPIR-V binding")
        seen.add((actual_set, binding_number))
        need(binding_number in bindings, "SPIR-V descriptor absent from DMVK")
        binding = bindings[binding_number]
        kind, values = type_at(target)
        elements = 1
        array_types = set()
        while kind == 28:
            need(target not in array_types, "Cyclic descriptor array type")
            array_types.add(target)
            need(len(values) == 2 and values[1] in constants and len(constants[values[1]]) == 1,
                 "Descriptor array needs a literal 32-bit length")
            length = constants[values[1]][0]
            need(1 <= length <= 128 and elements * length <= 128, "Invalid descriptor array length")
            elements *= length
            target = values[0]
            kind, values = type_at(target)
        need(elements == binding[7], "Descriptor array count differs from DMVK")
        if storage in (2, 12):
            need(kind == 30, "Buffer descriptor must point to a struct")
            storage_buffer = storage == 12 or (target, 3) in decorations
            need(binding[0] == 2 if storage_buffer else binding[0] in (0, 1), "Buffer kind differs from DMVK")
            need(storage_buffer or (target, 2) in decorations, "Uniform buffer missing Block")
            continue
        separate = bool(binding[9])
        if kind == 26:
            need(binding[0] == 3 and separate and binding[6] & 255 == 0, "Sampler differs from DMVK")
            continue
        combined = kind == 27
        if combined:
            need(len(values) == 1, "Invalid sampled image type")
            _, values = type_at(values[0], 25)
        else:
            need(kind == 25, "Unsupported descriptor type")
        need(len(values) >= 7, "Truncated image type")
        _, dimension, depth, arrayed, multisampled, sampled, _ = values[:7]
        image_type = binding[6]
        need(dimension == {1: 0, 2: 5, 3: 1, 4: 2, 5: 3}.get(image_type & 255),
             "Image dimension differs from DMVK")
        need(bool(arrayed) == bool(image_type & 256) and bool(multisampled) == bool(image_type & 512),
             "Image array/MS flags differ from DMVK")
        need(depth == 2 or bool(depth) == bool(image_type & 1024), "Image depth flag differs from DMVK")
        need(sampled in (1, 2) and binding[0] == (4 if sampled == 2 else 3), "Image kind differs from DMVK")
        need(not combined if sampled == 2 else dimension == 5 or combined != separate,
             "Image/sampler descriptor form differs from DMVK")
    if stage == 0:
        need(all(value == inputs.get(location, 0) for location, value in enumerate(attributes)),
             "Vertex attribute types/locations differ from DMVK")


def write_metadata(spirv, path, metadata):
    validate_pair(spirv, metadata)
    path.write_bytes(metadata)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("Usage: shader_pair_contract.py SPIRV_DIRECTORY")
    try:
        checked = 0
        for path in sorted(Path(sys.argv[1]).glob("*.meta")):
            validate_pair(path.with_suffix(".spv"), path.read_bytes())
            checked += 1
        need(checked > 0, "No shader pairs found")
        print(f"PASS SPIR-V/DMVK pairs={checked}")
    except (OSError, ValueError, Violation) as error:
        sys.exit(str(error))
