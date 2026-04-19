#include <verve/pmm.h>

#include <stddef.h>

#define FRAME_SHIFT 12u
#define FRAME_SIZE  (1u << FRAME_SHIFT)

extern char kernel_image_begin[];
extern char kernel_image_end[];

static uint8_t *bitmap;
static uint32_t bitmap_byte_len;
static uint32_t frame_count;

static inline uintptr_t align_up(uintptr_t x, uintptr_t a)
{
    return (x + a - 1u) & ~(a - 1u);
}

static inline uintptr_t align_down(uintptr_t x, uintptr_t a)
{
    return x & ~(a - 1u);
}

static inline uint32_t phys_pfn(uintptr_t p)
{
    return (uint32_t)(p >> FRAME_SHIFT);
}

static void bm_set(uint32_t pfn)
{
    uint32_t i = pfn >> 3;
    uint32_t b = pfn & 7u;

    bitmap[i] = (uint8_t)(bitmap[i] | (uint8_t)(1u << b));
}

static void bm_clear(uint32_t pfn)
{
    uint32_t i = pfn >> 3;
    uint32_t b = pfn & 7u;

    bitmap[i] = (uint8_t)(bitmap[i] & (uint8_t)~(1u << b));
}

static int bm_get(uint32_t pfn)
{
    uint32_t i = pfn >> 3;
    uint32_t b = pfn & 7u;

    return (bitmap[i] >> b) & 1u;
}

static void bm_fill(uint8_t v, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        bitmap[i] = v;
}

static void frames_mark_used(uintptr_t begin, uintptr_t end)
{
    if (begin >= end)
        return;

    uintptr_t b = align_down(begin, FRAME_SIZE);
    uintptr_t e = align_up(end, FRAME_SIZE);

    for (uintptr_t p = b; p < e; p += FRAME_SIZE) {
        uint32_t pfn = phys_pfn(p);

        if (pfn < frame_count)
            bm_set(pfn);
    }
}

static void frames_mark_free(uintptr_t begin, uintptr_t end)
{
    if (begin >= end)
        return;

    uintptr_t b = align_down(begin, FRAME_SIZE);
    uintptr_t e = align_up(end, FRAME_SIZE);

    for (uintptr_t p = b; p < e; p += FRAME_SIZE) {
        uint32_t pfn = phys_pfn(p);

        if (pfn < frame_count)
            bm_clear(pfn);
    }
}

struct max_ctx {
    uint64_t max_end;
};

static void max_cb(uint64_t base, uint64_t len, void *ctx)
{
    struct max_ctx *m = ctx;
    uint64_t end = base + len;

    if (end > m->max_end)
        m->max_end = end;
}

static void free_cb(uint64_t base, uint64_t len, void *ctx)
{
    (void)ctx;

    frames_mark_free((uintptr_t)base, (uintptr_t)(base + len));
}

bool pmm_init(const struct mb2_fixed *info, uint32_t mb_info_phys)
{
    struct max_ctx mx = {0};

    if (!mb2_for_each_available(info, max_cb, &mx) || mx.max_end == 0)
        return false;

    uint64_t last_byte = mx.max_end - 1u;
    uint32_t last_pfn = (uint32_t)(last_byte >> FRAME_SHIFT);

    frame_count = last_pfn + 1u;
    bitmap_byte_len = (frame_count + 7u) / 8u;

    uintptr_t bm_base = align_up((uintptr_t)kernel_image_end, FRAME_SIZE);

    bitmap = (uint8_t *)bm_base;
    bm_fill(0xFF, bitmap_byte_len);

    if (!mb2_for_each_available(info, free_cb, NULL))
        return false;

    uintptr_t k0 = (uintptr_t)kernel_image_begin;
    uintptr_t k1 = (uintptr_t)kernel_image_end;

    frames_mark_used(k0, k1);

    uintptr_t mb0 = (uintptr_t)mb_info_phys;
    uintptr_t mb1 = mb0 + (uintptr_t)info->total_size;

    frames_mark_used(mb0, mb1);

    uintptr_t bb0 = bm_base;
    uintptr_t bb1 = bm_base + (uintptr_t)bitmap_byte_len;

    frames_mark_used(bb0, bb1);

    bm_set(0);

    return true;
}

uint32_t pmm_frame_count(void)
{
    return frame_count;
}

uint32_t pmm_max_pfn(void)
{
    return frame_count ? frame_count - 1u : 0u;
}

uintptr_t pmm_alloc_frame(void)
{
    for (uint32_t pfn = 1; pfn < frame_count; ++pfn) {
        if (bm_get(pfn) == 0) {
            bm_set(pfn);
            return ((uintptr_t)pfn) << FRAME_SHIFT;
        }
    }

    return 0;
}

void pmm_free_frame(uintptr_t phys)
{
    if ((phys & (FRAME_SIZE - 1u)) != 0)
        return;

    uint32_t pfn = phys_pfn(phys);

    if (pfn == 0 || pfn >= frame_count)
        return;

    if (bm_get(pfn) == 0)
        return;

    bm_clear(pfn);
}
