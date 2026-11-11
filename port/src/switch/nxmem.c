/*
 * nxmem.c — fixed-address memory for the Nintendo Switch build. See nxmem.h.
 */
#include "platform.h"

#if defined(PLATFORM_SWITCH)

#include <malloc.h>
#include <string.h>
#include <switch.h>

#include "nxmem.h"
#include "system.h"

#define NX_PAGE       0x1000ULL
#define NX_MAX_BLOCKS 32

enum { NX_NONE = 0, NX_STACKMAP, NX_CODEMEM, NX_ALIAS };

typedef struct {
    uintptr_t addr;
    size_t size;
    void *src;   /* heap backing (NX_STACKMAP / NX_CODEMEM) */
    uintptr_t primary;   /* NX_ALIAS: the code-memory block it views */
    int kind;
} NxBlock;

int g_nxDramK0Folded = 0;

static NxBlock s_blocks[NX_MAX_BLOCKS];
static int s_init = 0;
static u64 s_stackBase, s_stackSize;
static Handle s_self = INVALID_HANDLE;

static void nxInit(void)
{
    if (s_init) {
        return;
    }
    s_init = 1;
    svcGetInfo(&s_stackBase, InfoType_StackRegionAddress, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&s_stackSize, InfoType_StackRegionSize, CUR_PROCESS_HANDLE, 0);
    s_self = envGetOwnProcessHandle();
    sysLogPrintf(LOG_NOTE, "nxmem: stack region 0x%010lx-0x%010lx, own process handle %s",
                 s_stackBase, s_stackBase + s_stackSize,
                 s_self != INVALID_HANDLE ? "available" : "MISSING (not title takeover / hbloader?)");
}

static NxBlock *nxSlot(void)
{
    for (int i = 0; i < NX_MAX_BLOCKS; i++) {
        if (s_blocks[i].kind == NX_NONE) {
            return &s_blocks[i];
        }
    }
    return NULL;
}

static int nxRangeFree(uintptr_t a, size_t s)
{
    while (s) {
        MemoryInfo mi;
        u32 pi;
        if (R_FAILED(svcQueryMemory(&mi, &pi, a)) || mi.type != MemType_Unmapped) {
            return 0;
        }
        u64 end = mi.addr + mi.size;
        if (end >= a + s) {
            return 1;
        }
        s -= end - a;
        a = end;
    }
    return 1;
}

static const char *nxDescribe(uintptr_t a)
{
    MemoryInfo mi;
    u32 pi;
    static char buf[96];
    if (R_FAILED(svcQueryMemory(&mi, &pi, a))) {
        return "query failed";
    }
    snprintf(buf, sizeof(buf), "type 0x%x block 0x%010lx+0x%lx", (unsigned)mi.type, mi.addr, mi.size);
    return buf;
}

void *nxMapFixed(uintptr_t addr, size_t size)
{
    nxInit();
    size = (size + NX_PAGE - 1) & ~(size_t)(NX_PAGE - 1);
    NxBlock *b = nxSlot();
    if (!b) {
        sysLogPrintf(LOG_ERROR, "nxmem: out of block slots");
        return NULL;
    }
    if (!nxRangeFree(addr, size)) {
        sysLogPrintf(LOG_ERROR, "nxmem: 0x%010lx+0x%lx is not free (%s)", addr, size, nxDescribe(addr));
        return NULL;
    }
    void *src = memalign(NX_PAGE, size);
    if (!src) {
        sysLogPrintf(LOG_ERROR, "nxmem: out of heap for 0x%lx bytes", size);
        return NULL;
    }
    memset(src, 0, size);

    Result rc;
    int inStack = addr >= s_stackBase && addr + size <= s_stackBase + s_stackSize;
    virtmemLock();
    if (inStack) {
        rc = svcMapMemory((void *)addr, src, size);
        if (R_SUCCEEDED(rc)) {
            b->kind = NX_STACKMAP;
        }
    } else if (s_self != INVALID_HANDLE) {
        rc = svcMapProcessCodeMemory(s_self, addr, (u64)src, size);
        if (R_SUCCEEDED(rc)) {
            rc = svcSetProcessMemoryPermission(s_self, addr, size, Perm_Rw);
            if (R_FAILED(rc)) {
                svcUnmapProcessCodeMemory(s_self, addr, (u64)src, size);
            } else {
                b->kind = NX_CODEMEM;
            }
        }
    } else {
        rc = MAKERESULT(Module_Libnx, LibnxError_BadInput);
    }
    virtmemUnlock();

    if (b->kind == NX_NONE) {
        sysLogPrintf(LOG_ERROR, "nxmem: could not map 0x%010lx+0x%lx via %s (rc=0x%x)", addr, size,
                     inStack ? "svcMapMemory" : "code memory", rc);
        free(src);
        return NULL;
    }
    b->addr = addr;
    b->size = size;
    b->src = src;
    sysLogPrintf(LOG_NOTE, "nxmem: mapped 0x%010lx+0x%lx (%s)", addr, size,
                 b->kind == NX_STACKMAP ? "stack region" : "code memory");
    return (void *)addr;
}

void *nxMapAlias(uintptr_t view, uintptr_t primary, size_t size)
{
    nxInit();
    size = (size + NX_PAGE - 1) & ~(size_t)(NX_PAGE - 1);
    NxBlock *b = nxSlot();
    if (!b || s_self == INVALID_HANDLE || !nxRangeFree(view, size)) {
        return NULL;
    }
    virtmemLock();
    Result rc = svcMapProcessMemory((void *)view, s_self, primary, size);
    virtmemUnlock();
    if (R_FAILED(rc)) {
        sysLogPrintf(LOG_NOTE, "nxmem: alias 0x%010lx -> 0x%010lx refused (rc=0x%x)", view, primary, rc);
        return NULL;
    }
    b->kind = NX_ALIAS;
    b->addr = view;
    b->size = size;
    b->primary = primary;
    sysLogPrintf(LOG_NOTE, "nxmem: alias 0x%010lx -> 0x%010lx (+0x%lx)", view, primary, size);
    return (void *)view;
}

void nxUnmapFixed(uintptr_t addr)
{
    for (int i = 0; i < NX_MAX_BLOCKS; i++) {
        NxBlock *b = &s_blocks[i];
        if (b->kind == NX_NONE || b->addr != addr) {
            continue;
        }
        Result rc = 0;
        virtmemLock();
        if (b->kind == NX_STACKMAP) {
            rc = svcUnmapMemory((void *)b->addr, b->src, b->size);
        } else if (b->kind == NX_CODEMEM) {
            rc = svcUnmapProcessCodeMemory(s_self, b->addr, (u64)b->src, b->size);
        } else if (b->kind == NX_ALIAS) {
            rc = svcUnmapProcessMemory((void *)b->addr, s_self, b->primary, b->size);
        }
        virtmemUnlock();
        if (R_FAILED(rc)) {
            sysLogPrintf(LOG_ERROR, "nxmem: unmap 0x%010lx failed (rc=0x%x); leaking it", b->addr, rc);
        } else if (b->src) {
            free(b->src);
        }
        memset(b, 0, sizeof(*b));
        return;
    }
}

void nxLogThreadCore(const char *name, int id)
{
    s32 prio = -1;
    s32 core = -1;
    u64 mask = 0;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    svcGetThreadCoreMask(&core, &mask, CUR_THREAD_HANDLE);
    sysLogPrintf(LOG_NOTE, "nxthread: %s %d on core %u (ideal %d, mask 0x%lx, prio 0x%x)", name, id,
                 svcGetCurrentProcessorNumber(), core, mask, prio);
}

#endif /* PLATFORM_SWITCH */
