// Test-only link substitutions for the SM GetServiceHandle publication
// rollback fixture. The production object files are not modified.
#include "../runtime/internal.h"
#include "../runtime/host.h"
#include "../runtime/ipc.h"
#include <errno.h>
#include <stdint.h>

int test_output_pin_fail;
dm_host *test_output_pin_host;
uintptr_t test_output_pin_tls;
int test_output_pin_calls;
int test_output_pin_injections;
uint32_t test_created_handle;

// Compiled from runtime/ipc.c with dm_host_ipc_create_session renamed here.
// Record the real session handle before the transport attempts publication.
int test_create_session(dm_host *host, const char *name, uint32_t *out_handle) {
    int rc = dm_host_ipc_create_session(host, name, out_handle);
    if (rc == 0 && out_handle && *out_handle) test_created_handle = *out_handle;
    return rc;
}

// Compiled from runtime/ipc.c with dm_host_memory_pin_locked renamed here.
// Delegate every pin except the exact armed GetServiceHandle output pin.
int test_output_pin(dm_host *host, dm_memory_pin *pin, uintptr_t address, size_t size) {
    test_output_pin_calls++;
    if (test_output_pin_fail && host == test_output_pin_host &&
        address == test_output_pin_tls && size == DM_IPC_BUFFER_SIZE) {
        test_output_pin_fail = 0;
        test_output_pin_injections++;
        return EACCES;
    }
    return dm_host_memory_pin_locked(host, pin, address, size);
}
