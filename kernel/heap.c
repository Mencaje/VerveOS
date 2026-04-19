#include <verve/heap.h>

#include <stddef.h>
#include <stdint.h>

/*
 * First-fit freelist over a BSS arena plus bump allocator for misses.
 */

#define VERVE_HEAP_SIZE ((size_t)(256 * 1024))

typedef struct heap_hdr {
    size_t total;
} heap_hdr;

#define HEAP_HDR_ALIGN      16u
#define HEAP_HDR_USER_OFFSET                                            \
    ((sizeof(heap_hdr) + (HEAP_HDR_ALIGN - 1)) & ~(HEAP_HDR_ALIGN - 1))

static uint8_t g_heap[VERVE_HEAP_SIZE] __attribute__((aligned(HEAP_HDR_ALIGN)));

static heap_hdr *g_freelist;

static size_t g_heap_off;

static size_t align_up(size_t x, size_t align)
{
    return (x + align - 1u) & ~(align - 1u);
}

void heap_init(void)
{
    g_freelist = NULL;
    g_heap_off = 0;
}

void *kmalloc(size_t size)
{
    size_t need;

    if (size == 0 || size > VERVE_HEAP_SIZE)
        return NULL;

    need = HEAP_HDR_USER_OFFSET + size;
    need = align_up(need, HEAP_HDR_ALIGN);

    if (need > VERVE_HEAP_SIZE)
        return NULL;

    {
        heap_hdr **plink = &g_freelist;
        heap_hdr *b;

        for (b = g_freelist; b != NULL;) {
            heap_hdr *nb = *(heap_hdr **)((uint8_t *)b + HEAP_HDR_USER_OFFSET);

            if (b->total >= need) {
                *plink = nb;
                return (void *)((uint8_t *)b + HEAP_HDR_USER_OFFSET);
            }

            plink = (heap_hdr **)((uint8_t *)b + HEAP_HDR_USER_OFFSET);
            b = nb;
        }
    }

    if (VERVE_HEAP_SIZE - g_heap_off < need)
        return NULL;

    {
        heap_hdr *h = (heap_hdr *)(g_heap + g_heap_off);

        h->total = need;
        g_heap_off += need;

        return (void *)((uint8_t *)h + HEAP_HDR_USER_OFFSET);
    }
}

void kfree(void *ptr)
{
    uintptr_t ua;
    uintptr_t arena_lo;
    uintptr_t arena_hi;
    heap_hdr *h;

    if (!ptr)
        return;

    ua = (uintptr_t)ptr;

    arena_lo = (uintptr_t)g_heap + HEAP_HDR_USER_OFFSET;
    arena_hi = (uintptr_t)g_heap + VERVE_HEAP_SIZE;

    if (ua < arena_lo || ua >= arena_hi)
        return;

    h = (heap_hdr *)((uint8_t *)ptr - HEAP_HDR_USER_OFFSET);

    *(heap_hdr **)((uint8_t *)h + HEAP_HDR_USER_OFFSET) = g_freelist;
    g_freelist = h;
}
