#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MB2_MAGIC 0x36d76289u

#define MB2_TAG_END          0
#define MB2_TAG_CMDLINE      1
#define MB2_TAG_BOOTLOADER   2
#define MB2_TAG_BASIC_MEMINFO 4
#define MB2_TAG_MMAP         6
#define MB2_TAG_ACPI_OLD     14
#define MB2_TAG_ACPI_NEW     15

#define MB2_MMAP_AVAILABLE 1

struct mb2_fixed {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct mb2_mmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t reserved0;
} __attribute__((packed));

typedef void (*mb2_avail_cb)(uint64_t base, uint64_t len, void *ctx);

bool mb2_sum_usable_ram(const struct mb2_fixed *info, uint64_t *usable_out);

bool mb2_read_basic_meminfo(const struct mb2_fixed *info,
    uint32_t *mem_lower_kb_out, uint32_t *mem_upper_kb_out);

bool mb2_for_each_available(const struct mb2_fixed *info, mb2_avail_cb fn,
    void *ctx);

bool mb2_find_rsdp_payload(const struct mb2_fixed *info, uint8_t *out_buf,
    size_t out_cap, size_t *out_len);
