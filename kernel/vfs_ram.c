#include <verve/paging.h>
#include <verve/serial.h>
#include <verve/vfs_ram.h>

#include <stdint.h>

/*
 * Tiny read-only ramfs (no libc). Built-in names at compile time.
 */

#define VFS_MAX_PATH   96u
#define VFS_MAX_UFD    12u
#define VFS_FD_BASE    3u

typedef struct {
    const char *path;
    const char *data;
    uint32_t len;
} vfs_ent_t;

static const char s_hello_data[] = "Hello from VerveOS ramfs (vfs_ram.c).\r\n";

/*
 * Flat Ring-3 blob: mov eax,SYS_EXIT; xor ebx,ebx; int 0x80
 * (kernel/exec_flat.c can iret into this without an on-disk toolchain).
 */
static const uint8_t s_exit_bin[] = {
    0xB8, 0x03, 0x00, 0x00, 0x00,
    0x31, 0xDB,
    0xCD, 0x80,
};

static const vfs_ent_t g_vfs_files[] = {
    {
        "/hello.txt",
        s_hello_data,
        (uint32_t)(sizeof(s_hello_data) - 1u),
    },
    {
        "/exit.bin",
        (const char *)(uintptr_t)s_exit_bin,
        (uint32_t)sizeof(s_exit_bin),
    },
};

#define VFS_FILE_COUNT ((uint32_t)(sizeof(g_vfs_files) / sizeof(g_vfs_files[0])))

struct vfs_ufd {
    uint8_t used;
    uint8_t vfidx;
    uint32_t pos;
};

static struct vfs_ufd g_ufd[VFS_MAX_UFD];

static int strcmp_v(const char *a, const char *b)
{
    while (*a && *a == *b) {
        ++a;
        ++b;
    }

    return (int)(unsigned char)(*a) - (int)(unsigned char)(*b);
}

static int copy_user_path(uint32_t uva, char *out, size_t outsz)
{
    size_t i;

    if (outsz == 0)
        return -1;

    for (i = 0; i < outsz - 1u; ++i) {
        char c;

        if (!paging_user_range_ok((uintptr_t)(uva + (uint32_t)i), 1u, 0))
            return -1;

        c = *(const volatile char *)(uintptr_t)(uva + i);
        out[i] = c;

        if (c == '\0')
            return 0;
    }

    out[outsz - 1u] = '\0';
    return 0;
}

static int32_t vfs_find(const char *path)
{
    uint32_t i;

    for (i = 0; i < VFS_FILE_COUNT; ++i) {
        if (strcmp_v(path, g_vfs_files[i].path) == 0)
            return (int32_t)i;
    }

    return -1;
}

int32_t verve_vfs_get_file_k(const char *path, const uint8_t **out_data,
    uint32_t *out_len)
{
    int32_t ix;

    ix = vfs_find(path);

    if (ix < 0)
        return -2;

    *out_data = (const uint8_t *)(uintptr_t)g_vfs_files[(uint32_t)ix].data;
    *out_len = g_vfs_files[(uint32_t)ix].len;

    return 0;
}

static int32_t ufd_alloc(uint8_t vfidx)
{
    uint32_t s;

    for (s = 0; s < VFS_MAX_UFD; ++s) {
        if (g_ufd[s].used == 0u) {
            g_ufd[s].used = 1u;
            g_ufd[s].vfidx = vfidx;
            g_ufd[s].pos = 0;
            return (int32_t)(VFS_FD_BASE + (int32_t)s);
        }
    }

    return -24; /* EMFILE */
}

void verve_vfs_init(void)
{
    uint32_t i;

    for (i = 0; i < VFS_MAX_UFD; ++i) {
        g_ufd[i].used = 0;
        g_ufd[i].vfidx = 0;
        g_ufd[i].pos = 0;
    }

    serial_puts("[VerveOS] vfs_ram: ");
    serial_put_u64_hex((uint64_t)VFS_FILE_COUNT);
    serial_puts(" static file(s)\r\n");
}

static struct vfs_ufd *vfs_get_ufd(int32_t fd)
{
    int32_t slot;

    if (fd < (int32_t)VFS_FD_BASE)
        return NULL;

    slot = fd - (int32_t)VFS_FD_BASE;

    if (slot < 0 || slot >= (int32_t)VFS_MAX_UFD)
        return NULL;

    if (g_ufd[slot].used == 0u)
        return NULL;

    return &g_ufd[slot];
}

int32_t verve_vfs_open_u(uint32_t path_va)
{
    char kpath[VFS_MAX_PATH];
    int32_t vfid;
    int cop;

    cop = copy_user_path(path_va, kpath, sizeof kpath);

    if (cop != 0)
        return -14; /* EFAULT */

    vfid = vfs_find(kpath);

    if (vfid < 0)
        return -2; /* ENOENT */

    return ufd_alloc((uint8_t)vfid);
}

int32_t verve_vfs_read_u(int32_t fd, uint32_t buf_va, uint32_t len)
{
    struct vfs_ufd *u;
    const vfs_ent_t *fe;
    uint32_t want;
    uint32_t got;
    uint32_t i;
    uint32_t p;
    uint32_t n;

    u = vfs_get_ufd(fd);

    if (!u)
        return -9; /* EBADF */

    fe = &g_vfs_files[u->vfidx];

    if (len == 0)
        return 0;

    if (len > 4096u)
        len = 4096u;

    if (!paging_user_range_ok((uintptr_t)buf_va, (size_t)len, 1))
        return -14;

    if (u->pos >= fe->len)
        return 0;

    want = fe->len - u->pos;

    if (want > len)
        want = len;

    got = want;
    p = u->pos;
    u->pos += got;

    n = got;
    for (i = 0; i < n; ++i) {
        char c = fe->data[p + i];
        volatile char *d = (volatile char *)(uintptr_t)(buf_va + i);

        *d = c;
    }

    return (int32_t)got;
}

void verve_vfs_close_u(int32_t fd)
{
    struct vfs_ufd *u;

    if (fd < (int32_t)VFS_FD_BASE)
        return;

    u = vfs_get_ufd(fd);

    if (!u)
        return;

    u->used = 0;
}

/* --- kernel-space selftest (no Ring 3) ---------------------------------- */

static int32_t vfs_open_k(const char *path)
{
    int32_t vfid;

    vfid = vfs_find(path);

    if (vfid < 0)
        return -2;

    return ufd_alloc((uint8_t)vfid);
}

static int32_t vfs_read_k(int32_t fd, char *kbuf, uint32_t len)
{
    struct vfs_ufd *u;
    const vfs_ent_t *fe;
    uint32_t want;
    uint32_t got;
    uint32_t i;
    uint32_t p;

    u = vfs_get_ufd(fd);

    if (!u)
        return -9;

    fe = &g_vfs_files[u->vfidx];

    if (len == 0 || u->pos >= fe->len)
        return 0;

    want = fe->len - u->pos;

    if (want > len)
        want = len;

    got = want;
    p = u->pos;
    u->pos += got;

    for (i = 0; i < got; ++i)
        kbuf[i] = fe->data[p + i];

    return (int32_t)got;
}

void verve_vfs_selftest(void)
{
    char buf[48];
    int32_t fd;
    int32_t n;

    fd = vfs_open_k("/hello.txt");

    if (fd < 0) {
        serial_puts("[VerveOS] vfs selftest: open failed\r\n");
        return;
    }

    n = vfs_read_k(fd, buf, sizeof buf - 1u);

    if (n <= 0) {
        serial_puts("[VerveOS] vfs selftest: read failed\r\n");
        verve_vfs_close_u(fd);
        return;
    }

    buf[(uint32_t)n < sizeof buf ? (uint32_t)n : sizeof buf - 1u] = '\0';

    serial_puts("[VerveOS] vfs selftest read: ");
    serial_puts(buf);

    verve_vfs_close_u(fd);
}
