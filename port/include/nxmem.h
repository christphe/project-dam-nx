#ifndef PORT_NXMEM_H
#define PORT_NXMEM_H
/*
 * nxmem — fixed-address memory for the Nintendo Switch build (project-dam).
 *
 * Horizon has no mmap(MAP_FIXED). This places page-aligned heap memory at an
 * exact virtual address, the way the PC port's fixed mmaps do, using the two
 * methods verified on hardware by tools/switch/memprobe (docs/switch/PLAN.md
 * §2):
 *   - inside the process's stack region: svcMapMemory (heap -> stack region);
 *   - elsewhere in the ASLR region: svcMapProcessCodeMemory on the own process
 *     handle, then svcSetProcessMemoryPermission(RW).
 * Used for the cart image (0x10000000), the DRAM arena (0x70000000 + KSEG0
 * mirror) and the game-thread stacks (0xA0000000).
 */
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Map `size` bytes (rounded up to a page) of fresh zeroed memory at exactly
 * `addr`. Returns (void *)addr, or NULL (and logs why). */
void *nxMapFixed(uintptr_t addr, size_t size);

/* Map a second view of an existing nxMapFixed() block `primary` at `view`
 * (svcMapProcessMemory). Only works when `primary` was placed as code memory.
 * Returns (void *)view or NULL. */
void *nxMapAlias(uintptr_t view, uintptr_t primary, size_t size);

/* Undo nxMapFixed()/nxMapAlias() for the block that starts at `addr`. */
void nxUnmapFixed(uintptr_t addr);

/* Diagnostics: log which CPU core / priority the calling thread runs on. */
void nxLogThreadCore(const char *name, int id);

/* Set when the DRAM KSEG0 view (0x80000000) could not be mapped as a real
 * mirror: portN64ToHost() then folds KSEG0 addresses onto V1 in software. */
extern int g_nxDramK0Folded;

#ifdef __cplusplus
}
#endif

#endif /* PORT_NXMEM_H */
