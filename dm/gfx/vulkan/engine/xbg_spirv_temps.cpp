// 翻译器的单写临时变量直接转发。着色器库里每个中间值都放在自己的 Function 变量里：写一次、在同一个块里读若干次。驱动的编译器
// 自己也会消掉它们（对最重的着色器做 spirv-opt -O 测不出差别）；但在 xbg_spirv_ldc.cpp 的分量折叠之后再做，折出的常量就直接
// 落进访问链，Redmi K50 Ultra 营地 GPU 每帧少 1.4 ms。这里对“无初值、只被整写一次、其余只被读取、写与所有读在同一块且写在前、
// 没有名字或修饰”的变量，把每次读取的使用改成所写的值，删去变量、写与读。语义不变。
// The translator's single-store temporaries, forwarded. The shader library keeps every intermediate value in its own Function
// variable: stored once, then loaded in the same block. The driver's compiler removes them itself (spirv-opt -O on the heaviest
// shaders changed nothing measurable), but done after the component fold of xbg_spirv_ldc.cpp, the folded constants reach the
// access chains directly, and the Redmi K50 Ultra camp took 1.4 ms less GPU time a frame. For a variable with no initializer,
// stored exactly once by a whole store and otherwise only loaded, with the store and every load in one block and the store
// first, and with no name or decoration, every use of a load reads the stored value instead, and the variable, the store and
// the loads are dropped. The module computes exactly the same values.
#include "xbg_spirv_temps.h"

namespace {
enum : uint32_t {
    OpName = 5, OpMemberName = 6, OpDecorate = 71, OpMemberDecorate = 72, OpFunction = 54, OpFunctionEnd = 56,
    OpVariable = 59, OpLoad = 61, OpStore = 62, OpLabel = 248, StorageFunction = 7,
};
// Word w (1 = the first after the opcode) of a function-body instruction holds a literal, not an id.
bool literal_at(uint32_t op, uint32_t w) {
    switch (op) {
    case 8: return w >= 2;                       // OpLine: file id, then line / column
    case 12: return w == 4;                      // OpExtInst: the instruction number
    case OpVariable: return w == 3;              // storage class
    case OpLoad: return w >= 4;                  // memory operands
    case 62: case 63: return w >= 3;             // OpStore, OpCopyMemory: memory operands
    case 79: case 82: return w >= 5;             // OpVectorShuffle components, OpCompositeInsert indexes
    case 81: return w >= 4;                      // OpCompositeExtract indexes
    case 246: return w >= 3;                     // OpLoopMerge: loop control
    case 247: return w >= 2;                     // OpSelectionMerge: selection control
    case 250: return w >= 4;                     // OpBranchConditional: branch weights
    case 251: return w >= 3;                     // OpSwitch: case literals (and labels)
    // Image instructions: only the image-operands mask is a literal; the dref, gather component and operand values are ids.
    case 87: case 88: case 91: case 92: case 95: case 98: return w == 5;   // sample, fetch, read
    case 89: case 90: case 93: case 94: case 96: case 97: return w == 6;   // dref sample, gather, dref gather
    case 99: return w == 4;                      // OpImageWrite
    default: return false;
    }
}
}

size_t xbg_spirv_forward_temps(const uint32_t *in, size_t n, std::vector<uint32_t> &out) {
    out.clear();
    if (!in || n < 5 || in[0] != 0x07230203u) return 0;
    const uint32_t bound = in[3];
    // State per Function variable: 0 not one, 1 candidate (no store yet), 2 stored once, 3 rejected.
    std::vector<uint8_t> state(bound, 0);
    std::vector<uint32_t> value(bound, 0), store_block(bound, 0), repl(bound, 0);
    size_t first_func = 0;
    for (size_t i = 5; i < n;) {
        uint32_t wc = in[i] >> 16, op = in[i] & 0xffff;
        if (!wc || i + wc > n) return 0;
        if (op == OpFunction && !first_func) first_func = i;
        if (op == OpVariable && wc >= 4 && in[i + 2] < bound && in[i + 3] == StorageFunction) state[in[i + 2]] = wc == 4 ? 1 : 3;
        i += wc;
    }
    if (!first_func) return 0;
    // Named or decorated variables and loads stay (a name or decoration would be left pointing at nothing).
    std::vector<uint8_t> named(bound, 0);
    for (size_t i = 5; i < first_func;) {
        uint32_t wc = in[i] >> 16, op = in[i] & 0xffff;
        if ((op == OpName || op == OpMemberName || op == OpDecorate || op == OpMemberDecorate) && wc >= 2 && in[i + 1] < bound) {
            named[in[i + 1]] = 1;
            if (state[in[i + 1]]) state[in[i + 1]] = 3;
        }
        i += wc;
    }
    // Pass 2: check every use, in order. A load must follow the store in the store's block; its result is then replaced by the
    // stored value (a load of a load already replaced reads through to the original value).
    uint32_t block = 0;
    std::vector<uint32_t> load_var(bound, 0);
    for (size_t i = first_func; i < n;) {
        uint32_t wc = in[i] >> 16, op = in[i] & 0xffff;
        if (op == OpLabel && wc >= 2) block = in[i + 1];
        if (op == OpStore && wc >= 3 && in[i + 1] < bound && state[in[i + 1]] && state[in[i + 1]] != 3) {
            uint32_t v = in[i + 1], x = in[i + 2];
            if (x < bound && state[x]) state[x] = 3;   // a variable's pointer stored as a value
            if (state[v] == 1 && wc == 3) { state[v] = 2; value[v] = x < bound && repl[x] ? repl[x] : x; store_block[v] = block; }
            else state[v] = 3;
        }
        else if (op == OpLoad && wc >= 4 && in[i + 3] < bound && state[in[i + 3]] && state[in[i + 3]] != 3) {
            uint32_t v = in[i + 3];
            if (state[v] != 2 || store_block[v] != block || wc != 4 || in[i + 2] >= bound || named[in[i + 2]]) state[v] = 3;
            else { repl[in[i + 2]] = value[v]; load_var[in[i + 2]] = v; }
        }
        else {
            for (uint32_t w = 1; w < wc; w++) {
                uint32_t id = in[i + w];
                if (id < bound && state[id] && !(op == OpVariable && w == 2) && !literal_at(op, w)) state[id] = 3;
            }
        }
        i += wc;
    }
    // A rejected variable keeps its loads; only loads of variables that pass are replaced.
    size_t removed = 0;
    for (uint32_t v = 0; v < bound; v++) if (state[v] == 2) removed++;
    if (!removed) return 0;
    for (uint32_t id = 0; id < bound; id++) if (load_var[id] && state[load_var[id]] != 2) repl[id] = 0;
    // A stored value may itself be a replaced load (of a variable that passed): follow the chain to the original value.
    auto resolve = [&](uint32_t id) { for (int k = 0; k < 64 && id < bound && repl[id]; k++) id = repl[id]; return id; };
    out.reserve(n);
    out.insert(out.end(), in, in + first_func);
    for (size_t i = first_func; i < n;) {
        uint32_t wc = in[i] >> 16, op = in[i] & 0xffff;
        bool drop = (op == OpVariable && wc >= 4 && in[i + 2] < bound && state[in[i + 2]] == 2) ||
                    (op == OpStore && wc >= 3 && in[i + 1] < bound && state[in[i + 1]] == 2) ||
                    (op == OpLoad && wc >= 4 && in[i + 3] < bound && state[in[i + 3]] == 2);
        if (!drop) {
            size_t at = out.size();
            out.insert(out.end(), in + i, in + i + wc);
            for (uint32_t w = 1; w < wc; w++) {
                uint32_t id = in[i + w];
                if (id < bound && repl[id] && !literal_at(op, w)) out[at + w] = resolve(id);
            }
        }
        i += wc;
    }
    return removed;
}
