// 本地 sm: 服务端点；RegisterClient 与 GetServiceHandle 的已证明语义。
// Local sm: service endpoint; proven RegisterClient and GetServiceHandle semantics.
#pragma once
#include "host.h"
#include <stddef.h>

int dm_sm_install(dm_host *host);
void dm_sm_close(dm_host *host);
size_t dm_sm_registered_clients(dm_host *host);
