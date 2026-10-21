// 原生调用门按返回地址分派，TLS 与 x18 仅访问客体上下文；依据 gate.S 的完整寄存器帧约定。
// Dispatch native gates by return address; TLS / x18 access only guest context, following gate.S's full register frame contract.
#include "host.h"
#include "gate_frame.h"
#include <stdio.h>
#include <stdlib.h>

static void write_register(dm_guest_context *context, uint64_t *registers, unsigned reg, uint64_t value) {
    if (reg == 18) context->slots[DM_SLOT_X18] = value;
    // fp/lr 会被跳板的 ldp 再恢复；不能改门自身用于返回跳板的 x30。
    // The stub's ldp restores fp/lr again; never overwrite the gate's own x30 return to the stub.
    else if (reg == 29 || reg == 30) ((uint64_t *)((uint8_t *)registers + sizeof(dm_gate_frame)))[reg - 29] = value;
    else if (reg < 31) registers[reg] = value;
}

uint64_t gate_dispatch(uint64_t return_address, uint64_t *registers) {
    dm_guest_context *context = dm_guest_current();
    if (!context) { fputs("[dm] gate on unattached thread\n", stderr); abort(); }
    const dm_gate_table *table = context->gates;
    size_t first = 0, last = table->count;
    while (first < last) {
        size_t middle = first + (last - first) / 2;
        if (table->sites[middle].return_address < return_address) first = middle + 1;
        else last = middle;
    }
    if (first == table->count || table->sites[first].return_address != return_address) {
        fprintf(stderr, "[dm] unregistered gate return=%#llx\n", (unsigned long long)return_address); abort();
    }
    const dm_gate_site *site = &table->sites[first];
    unsigned reg = site->argument;
    switch (site->kind) {
    case DM_GATE_SVC: return dm_host_svc(context->host, reg, registers);
    case DM_GATE_TICKS: write_register(context, registers, reg, dm_host_ticks()); break;
    case DM_GATE_TLS_READ: write_register(context, registers, reg, context->slots[DM_SLOT_TLS]); break;
    case DM_GATE_TLR_READ: write_register(context, registers, reg, context->slots[DM_SLOT_TLR]); break;
    case DM_GATE_X18_ADDRESS: registers[reg] = (uintptr_t)&context->slots[DM_SLOT_X18]; break;
    case DM_GATE_TLS_WRITE:
        if (reg == 18) context->slots[DM_SLOT_TLS] = context->slots[DM_SLOT_X18];
        else if (reg == 31) context->slots[DM_SLOT_TLS] = 0;
        else if (reg == 29 || reg == 30)
            context->slots[DM_SLOT_TLS] = ((uint64_t *)((uint8_t *)registers + sizeof(dm_gate_frame)))[reg - 29];
        else context->slots[DM_SLOT_TLS] = registers[reg];
        break;
    }
    return registers[0];
}

void dm_thread_returned(void) { fputs("[dm] guest thread returned without ExitThread\n", stderr); abort(); }
void unimpl_c(void) { fputs("[dm] unresolved guest import\n", stderr); abort(); }
