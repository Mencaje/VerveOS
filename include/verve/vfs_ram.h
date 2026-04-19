#pragma once

#include <stddef.h>
#include <stdint.h>

void verve_vfs_init(void);

void verve_vfs_selftest(void);

int32_t verve_vfs_open_u(uint32_t path_va);

int32_t verve_vfs_read_u(int32_t fd, uint32_t buf_va, uint32_t len);

void verve_vfs_close_u(int32_t fd);

/*
 * Kernel lookup of a static ramfs object (path must match exactly).
 * Returns 0 on success.
 */
int32_t verve_vfs_get_file_k(const char *path, const uint8_t **out_data,
    uint32_t *out_len);
