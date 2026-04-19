#pragma once

/*
 * Load a flat i386 binary from built-in ramfs into the singleton user map and
 * iret to Ring 3. Does not return if the guest calls SYS_EXIT (see syscall.c).
 */
void verve_exec_flat_run(const char *path);
