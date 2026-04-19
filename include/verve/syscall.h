#pragma once

#include <verve/interrupt.h>

/* Linux-ish numbers for habits; extend as needed */
#define SYS_WRITE   1u
#define SYS_GETPID  2u
#define SYS_EXIT    3u
#define SYS_OPEN    5u
#define SYS_READ    6u
#define SYS_CLOSE   7u
#define SYS_BRK     45u

#define SYS_ENOSYS 38

void syscall_invoke(verve_exc_frame *f);
