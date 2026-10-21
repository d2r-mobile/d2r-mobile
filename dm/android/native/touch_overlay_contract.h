#pragma once
#include <stddef.h>

/* Optional original touchscreen RomFS overlay. Returns 1 valid, 0 absent, -1
 * malformed; `why` receives a concrete reason on failure. The host may expose
 * DM_ROMFS_OVERLAY only for a valid complete overlay. */
int dm_touch_overlay_contract(const char *dir, char *why, size_t n);

/* Validate and publish/unpublish the overlay path. Returns the contract code. */
int dm_touch_overlay_apply(const char *dir, char *why, size_t n);
