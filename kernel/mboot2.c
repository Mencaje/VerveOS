#include <verve/mboot2.h>

#include <stddef.h>

static bool walk_mmap_entries(const struct mb2_fixed *info,
    bool (*on_entry)(const struct mb2_mmap_entry *e, void *ctx), void *ctx)
{
    const uint8_t *base = (const uint8_t *)info;
    uint32_t total = info->total_size;

    if (total < 16 || (total & 7u) != 0)
        return false;

    const uint8_t *end = base + total;
    const uint8_t *p = base + sizeof(struct mb2_fixed);

    while (p + sizeof(struct mb2_tag) <= end) {
        const struct mb2_tag *tag = (const struct mb2_tag *)p;
        uint32_t sz = tag->size;

        if (sz < sizeof(struct mb2_tag) || (sz & 7u) != 0)
            return false;
        if (p + sz > end)
            return false;

        if (tag->type == MB2_TAG_END)
            break;

        if (tag->type == MB2_TAG_MMAP) {
            if (sz < 16 + sizeof(struct mb2_mmap_entry))
                return false;

            uint32_t entry_size = *(const uint32_t *)(p + 8);
            uint32_t entry_ver = *(const uint32_t *)(p + 12);
            (void)entry_ver;

            if (entry_size < sizeof(struct mb2_mmap_entry))
                return false;

            const uint8_t *e = p + 16;
            const uint8_t *tag_end = p + sz;

            while (e + entry_size <= tag_end) {
                const struct mb2_mmap_entry *m =
                    (const struct mb2_mmap_entry *)e;

                if (!on_entry(m, ctx))
                    return false;

                e += entry_size;
            }
        }

        p += sz;
    }

    return true;
}

struct sum_ctx {
    uint64_t sum;
};

static bool sum_cb(const struct mb2_mmap_entry *e, void *ctx)
{
    struct sum_ctx *s = ctx;

    if (e->type == MB2_MMAP_AVAILABLE)
        s->sum += e->length;

    return true;
}

bool mb2_sum_usable_ram(const struct mb2_fixed *info, uint64_t *usable_out)
{
    struct sum_ctx ctx = {0};

    if (!walk_mmap_entries(info, sum_cb, &ctx))
        return false;

    *usable_out = ctx.sum;
    return true;
}

bool mb2_read_basic_meminfo(const struct mb2_fixed *info,
    uint32_t *mem_lower_kb_out, uint32_t *mem_upper_kb_out)
{
    const uint8_t *base = (const uint8_t *)info;
    uint32_t total = info->total_size;

    if (total < 16 || (total & 7u) != 0)
        return false;

    const uint8_t *end = base + total;
    const uint8_t *p = base + sizeof(struct mb2_fixed);

    while (p + sizeof(struct mb2_tag) <= end) {
        const struct mb2_tag *tag = (const struct mb2_tag *)p;
        uint32_t sz = tag->size;

        if (sz < sizeof(struct mb2_tag) || (sz & 7u) != 0)
            return false;
        if (p + sz > end)
            return false;

        if (tag->type == MB2_TAG_END)
            break;

        if (tag->type == MB2_TAG_BASIC_MEMINFO) {
            if (sz < 16)
                return false;

            *mem_lower_kb_out = *(const uint32_t *)(p + 8);
            *mem_upper_kb_out = *(const uint32_t *)(p + 12);
            return true;
        }

        p += sz;
    }

    return false;
}

struct avail_ctx {
    mb2_avail_cb fn;
    void *user;
};

static bool avail_walk_cb(const struct mb2_mmap_entry *e, void *ctx)
{
    struct avail_ctx *ac = ctx;

    if (e->type == MB2_MMAP_AVAILABLE && e->length != 0)
        ac->fn(e->base, e->length, ac->user);

    return true;
}

bool mb2_for_each_available(const struct mb2_fixed *info, mb2_avail_cb fn,
    void *ctx)
{
    struct avail_ctx ac = {fn, ctx};

    return walk_mmap_entries(info, avail_walk_cb, &ac);
}

static bool find_acpi_tag(const struct mb2_fixed *info, uint32_t want_type,
    const uint8_t **payload_out, uint32_t *payload_len_out)
{
    const uint8_t *base = (const uint8_t *)info;
    uint32_t total = info->total_size;

    if (total < 16 || (total & 7u) != 0)
        return false;

    const uint8_t *end = base + total;
    const uint8_t *p = base + sizeof(struct mb2_fixed);

    while (p + sizeof(struct mb2_tag) <= end) {
        const struct mb2_tag *tag = (const struct mb2_tag *)p;
        uint32_t sz = tag->size;

        if (sz < sizeof(struct mb2_tag) || (sz & 7u) != 0)
            return false;
        if (p + sz > end)
            return false;

        if (tag->type == MB2_TAG_END)
            break;

        if (tag->type == want_type) {
            if (sz <= 8u)
                return false;

            *payload_out = p + 8u;
            *payload_len_out = sz - 8u;
            return true;
        }

        p += sz;
    }

    return false;
}

bool mb2_find_rsdp_payload(const struct mb2_fixed *info, uint8_t *out_buf,
    size_t out_cap, size_t *out_len)
{
    const uint8_t *pl;
    uint32_t plen;
    size_t n;

    if (!find_acpi_tag(info, MB2_TAG_ACPI_NEW, &pl, &plen) || pl == NULL
        || plen == 0) {
        if (!find_acpi_tag(info, MB2_TAG_ACPI_OLD, &pl, &plen)
            || pl == NULL || plen == 0) {
            *out_len = 0;
            return false;
        }
    }

    n = (size_t)plen;
    if (n > out_cap) {
        *out_len = 0;
        return false;
    }

    {
        size_t i;

        for (i = 0; i < n; ++i)
            out_buf[i] = pl[i];
    }

    *out_len = n;
    return true;
}
