/* heap.cpp -- malloc and its family, from the bodies at 0x004646A9 (free)
 * through 0x00467C09 (__sbh_resize_block).
 *
 * MSVC 6's allocator is two allocators. A request of 1016 bytes or less
 * (__sbh_threshold) goes to the small-block heap: regions of a reserved
 * megabyte, each committed in 32 KB groups of eight 4 KB pages, every
 * block carrying its size in front and behind (bit 0 set while it is in
 * use), free blocks on one of 64 doubly-linked lists per group by size
 * class, and three levels of bitmap -- per group, per region and a
 * count per class per region -- saying where a free entry of at least
 * a class can be found. Anything larger goes to HeapAlloc on the CRT's
 * own heap, rounded up to sixteen. Which allocator a pointer came from is
 * answered by __sbh_find_block, which asks whether it lies inside any
 * region's megabyte.
 *
 * The state is the image's: the header list and its counts, the scan and
 * defer pointers, the heap handle. Startup creates the heap and the
 * header list; the standalone and native builds run no startup, so the
 * first allocation does. The addresses handed out are then whatever the
 * platform's VirtualAlloc and HeapAlloc answer, which is what the hybrid's
 * original gets from the same platform -- src/hybrid/crtcheck.cpp compares
 * the two allocators on one shared state.
 */
#include "crt.h"

#include <stdio.h>
#include <stdlib.h>
#include "../../inject/win32.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#define G32(a)  (*(int32_t *)(uintptr_t)AM2_IMAGE(a))
#define GU32(a) (*(uint32_t *)(uintptr_t)AM2_IMAGE(a))
#define crt_sbh_threshold   GU32(ADDR_CRT_SBH_THRESHOLD)
#define crt_newmode         G32(ADDR_CRT_NEWMODE)
#define crt_pnhheap         (*(int32_t (__cdecl **)(uint32_t))(uintptr_t)AM2_IMAGE(ADDR_CRT_PNHHEAP))
#define crt_crtheap         (*(HANDLE *)(uintptr_t)AM2_IMAGE(ADDR_CRT_CRTHEAP))
#define crt_pHeaderList     (*(CRT_SBH_HEADER **)(uintptr_t)AM2_IMAGE(ADDR_CRT_SBH_PHEADER_LIST))
#define crt_cntHeaderList   G32(ADDR_CRT_SBH_CNT_HEADER_LIST)
#define crt_sizeHeaderList  G32(ADDR_CRT_SBH_SIZE_HEADER_LIST)
#define crt_pHeaderScan     (*(CRT_SBH_HEADER **)(uintptr_t)AM2_IMAGE(ADDR_CRT_SBH_PHEADER_SCAN))
#define crt_pHeaderDefer    (*(CRT_SBH_HEADER **)(uintptr_t)AM2_IMAGE(ADDR_CRT_SBH_PHEADER_DEFER))
#define crt_indGroupDefer   G32(ADDR_CRT_SBH_IND_GROUP_DEFER)

#define SBH_REGION_BYTES    0x100000
#define SBH_GROUP_BYTES     0x8000
#define SBH_PAGE_BYTES      0x1000
#define SBH_GROUPS          32
#define SBH_CLASSES         64
#define SBH_HEADER_STEP     16
#define SBH_REGION_ALLOC    0x41C4
#define SBH_HEADER_ALLOC    0x140

/* A free entry: its size, then the list links. A list head is addressed
 * as an entry whose links sit at +4 and +8, so `group + 8 * ind` names
 * class ind's head with the class's links at +4 and +8. */
typedef struct CRT_SBH_ENTRY {
    int32_t              sizeFront;
    struct CRT_SBH_ENTRY *pNext;
    struct CRT_SBH_ENTRY *pPrev;
} CRT_SBH_ENTRY;

typedef struct CRT_SBH_GROUP {
    int32_t cntEntries;
    struct { CRT_SBH_ENTRY *pNext, *pPrev; } listHead[SBH_CLASSES];
} CRT_SBH_GROUP;   /* 0x204 */

typedef struct CRT_SBH_REGION {
    int32_t       indGroupUse;
    uint8_t       cntRegionSize[SBH_CLASSES];
    uint32_t      bitvGroupHi[SBH_GROUPS];
    uint32_t      bitvGroupLo[SBH_GROUPS];
    CRT_SBH_GROUP grpHeadList[SBH_GROUPS];
} CRT_SBH_REGION;   /* 0x41C4 */

#define ENTRY_AT(p, off) ((CRT_SBH_ENTRY *)((uint8_t *)(p) + (off)))
#define HEAD(g, ind)     ENTRY_AT(g, 8 * (ind))
#define SIZE_INDEX(sz)   ({ int32_t i_ = ((int32_t)(sz) >> 4) - 1; i_ > 0x3F ? 0x3F : i_; })

static void sbh_clear_bits(CRT_SBH_HEADER *h, CRT_SBH_REGION *r, int32_t indGroup, int32_t ind)
{
    uint32_t bit;

    if (ind < 32) {
        bit = 0x80000000u >> ind;
        r->bitvGroupHi[indGroup] &= ~bit;
        if (--r->cntRegionSize[ind] == 0)
            h->bitvEntryHi &= ~bit;
    } else {
        bit = 0x80000000u >> (ind - 32);
        r->bitvGroupLo[indGroup] &= ~bit;
        if (--r->cntRegionSize[ind] == 0)
            h->bitvEntryLo &= ~bit;
    }
}

static void sbh_set_bits(CRT_SBH_HEADER *h, CRT_SBH_REGION *r, int32_t indGroup, int32_t ind)
{
    uint32_t bit;
    uint8_t  was = r->cntRegionSize[ind]++;

    if (ind < 32) {
        bit = 0x80000000u >> ind;
        if (was == 0)
            h->bitvEntryHi |= bit;
        r->bitvGroupHi[indGroup] |= bit;
    } else {
        bit = 0x80000000u >> (ind - 32);
        if (was == 0)
            h->bitvEntryLo |= bit;
        r->bitvGroupLo[indGroup] |= bit;
    }
}

static void sbh_unlink(CRT_SBH_ENTRY *e)
{
    e->pPrev->pNext = e->pNext;
    e->pNext->pPrev = e->pPrev;
}

/* At the head of class ind's list; the bits when the list was empty. */
static void sbh_link_head(CRT_SBH_HEADER *h, CRT_SBH_REGION *r, int32_t indGroup,
                          CRT_SBH_GROUP *g, int32_t ind, CRT_SBH_ENTRY *e)
{
    CRT_SBH_ENTRY *head = HEAD(g, ind);

    e->pNext = head->pNext;
    e->pPrev = head;
    head->pNext = e;
    e->pNext->pPrev = e;
    if (e->pNext == e->pPrev)
        sbh_set_bits(h, r, indGroup, ind);
}

int32_t __cdecl crt_sbh_heap_init(void)
{
    CRT_SBH_HEADER *list = (CRT_SBH_HEADER *)HeapAlloc(crt_crtheap, 0, SBH_HEADER_ALLOC);

    crt_pHeaderList = list;
    if (!list)
        return 0;
    crt_pHeaderDefer = NULL;
    crt_cntHeaderList = 0;
    crt_pHeaderScan = list;
    crt_sizeHeaderList = SBH_HEADER_STEP;
    return 1;
}

int32_t __cdecl crt_heap_init(int32_t mtflag)
{
    crt_crtheap = HeapCreate(mtflag == 0 ? HEAP_NO_SERIALIZE : 0, 0x1000, 0);
    if (!crt_crtheap)
        return 0;
    if (!crt_sbh_heap_init()) {
        HeapDestroy(crt_crtheap);
        return 0;
    }
    return 1;
}

/* Startup's job in the original; here the first request does it. */
static void ensure_heap(void)
{
    if (crt_crtheap == NULL)
        crt_heap_init(0);
}

CRT_SBH_HEADER *__cdecl crt_sbh_find_block(const void *p)
{
    CRT_SBH_HEADER *h = crt_pHeaderList;
    CRT_SBH_HEADER *end = h + crt_cntHeaderList;

    for (; h < end; h++)
        if ((uint32_t)((const uint8_t *)p - h->pHeapData) < SBH_REGION_BYTES)
            return h;
    return NULL;
}

CRT_SBH_HEADER *__cdecl crt_sbh_alloc_new_region(void)
{
    CRT_SBH_HEADER *h;
    CRT_SBH_REGION *r;

    if (crt_cntHeaderList == crt_sizeHeaderList) {
        CRT_SBH_HEADER *list = (CRT_SBH_HEADER *)HeapReAlloc(
            crt_crtheap, 0, crt_pHeaderList,
            (uint32_t)(crt_sizeHeaderList + SBH_HEADER_STEP) * sizeof(CRT_SBH_HEADER));

        if (!list)
            return NULL;
        crt_sizeHeaderList += SBH_HEADER_STEP;
        crt_pHeaderList = list;
    }
    h = crt_pHeaderList + crt_cntHeaderList;
    r = (CRT_SBH_REGION *)HeapAlloc(crt_crtheap, HEAP_ZERO_MEMORY, SBH_REGION_ALLOC);
    h->pRegion = r;
    if (!r)
        return NULL;
    h->pHeapData = (uint8_t *)VirtualAlloc(NULL, SBH_REGION_BYTES, MEM_RESERVE, PAGE_READWRITE);
    if (!h->pHeapData) {
        HeapFree(crt_crtheap, 0, r);
        return NULL;
    }
    h->bitvCommit = 0xFFFFFFFFu;
    h->bitvEntryHi = 0;
    h->bitvEntryLo = 0;
    crt_cntHeaderList++;
    r->indGroupUse = -1;
    return h;
}

int32_t __cdecl crt_sbh_alloc_new_group(CRT_SBH_HEADER *h)
{
    CRT_SBH_REGION *r = h->pRegion;
    CRT_SBH_GROUP  *g;
    uint32_t        bitv = h->bitvCommit;
    int32_t         ind = 0, i;
    uint8_t        *base, *page, *last;
    CRT_SBH_ENTRY  *head, *e;

    /* The first uncommitted group. */
    while (!(bitv & 0x80000000u)) {
        bitv <<= 1;
        ind++;
    }
    g = &r->grpHeadList[ind];
    /* Sixty-three of the list heads point at themselves; the last takes
     * the pages below. The loop starts at the count word, so head 0's
     * links are the first pair written. */
    for (i = 0; i < 63; i++) {
        head = HEAD(g, i);
        head->pPrev = head;
        head->pNext = head;
    }
    base = h->pHeapData + ind * SBH_GROUP_BYTES;
    if (!VirtualAlloc(base, SBH_GROUP_BYTES, MEM_COMMIT, PAGE_READWRITE))
        return -1;
    /* Eight pages, each one free entry of 0xFF0 bytes between sentinels
     * marked in-use, chained through the pages before and after. */
    last = base + 7 * SBH_PAGE_BYTES;
    for (page = base; page <= last; page += SBH_PAGE_BYTES) {
        e = ENTRY_AT(page, 0xC);
        *(int32_t *)(page + 8) = -1;
        *(int32_t *)(page + 0xFFC) = -1;
        e->sizeFront = 0xFF0;
        e->pNext = ENTRY_AT(page + SBH_PAGE_BYTES, 0xC);
        e->pPrev = ENTRY_AT(page - SBH_PAGE_BYTES, 0xC);
        *(int32_t *)(page + 0xFF8) = 0xFF0;
    }
    head = HEAD(g, 63);
    e = ENTRY_AT(base, 0xC);
    head->pNext = e;
    e->pPrev = head;
    e = ENTRY_AT(last, 0xC);
    head->pPrev = e;
    e->pNext = head;
    r->bitvGroupHi[ind] = 0;
    r->bitvGroupLo[ind] = 1;
    if (r->cntRegionSize[63]++ == 0)
        h->bitvEntryLo |= 1;
    h->bitvCommit &= ~(0x80000000u >> ind);
    return ind;
}

void *__cdecl crt_sbh_alloc_block(uint32_t n)
{
    CRT_SBH_HEADER *h, *end, *scan;
    CRT_SBH_REGION *r;
    CRT_SBH_GROUP  *g;
    CRT_SBH_ENTRY  *e, *alloc;
    int32_t         sizeEntry = (int32_t)((n + 0x17) & ~0xFu);
    int32_t         ind = (sizeEntry >> 4) - 1;
    uint32_t        maskHi, maskLo, bits;
    int32_t         indGroup, indEntry, sizeLeft, indLeft;

    end = crt_pHeaderList + crt_cntHeaderList;
    if (ind < 32) {
        maskHi = 0xFFFFFFFFu >> ind;
        maskLo = 0xFFFFFFFFu;
    } else {
        maskHi = 0;
        maskLo = 0xFFFFFFFFu >> (ind - 32);
    }

    /* A region with a free entry of the class or larger: from the scan
     * pointer to the end, then from the start to the scan pointer. */
    scan = crt_pHeaderScan;
    for (h = scan; h < end; h++)
        if ((h->bitvEntryHi & maskHi) | (h->bitvEntryLo & maskLo))
            break;
    if (h == end) {
        for (h = crt_pHeaderList; h < scan; h++)
            if ((h->bitvEntryHi & maskHi) | (h->bitvEntryLo & maskLo))
                break;
        if (h == scan) {
            /* None: a region with an uncommitted group, the same way
             * round, else a new region; then a new group in it. */
            for (h = scan; h < end; h++)
                if (h->bitvCommit != 0)
                    break;
            if (h == end) {
                for (h = crt_pHeaderList; h < scan; h++)
                    if (h->bitvCommit != 0)
                        break;
                if (h == scan) {
                    h = crt_sbh_alloc_new_region();
                    if (!h)
                        return NULL;
                }
            }
            h->pRegion->indGroupUse = crt_sbh_alloc_new_group(h);
            if (h->pRegion->indGroupUse == -1)
                return NULL;
        }
    }
    crt_pHeaderScan = h;
    r = h->pRegion;

    /* The group last used, if it can serve; else the first that can. */
    indGroup = r->indGroupUse;
    if (indGroup == -1
        || !((r->bitvGroupHi[indGroup] & maskHi) | (r->bitvGroupLo[indGroup] & maskLo))) {
        indGroup = 0;
        while (!((r->bitvGroupHi[indGroup] & maskHi) | (r->bitvGroupLo[indGroup] & maskLo)))
            indGroup++;
    }
    g = &r->grpHeadList[indGroup];

    /* The smallest class at or above ind with a free entry. */
    bits = r->bitvGroupHi[indGroup] & maskHi;
    indEntry = 0;
    if (bits == 0) {
        bits = r->bitvGroupLo[indGroup] & maskLo;
        indEntry = 32;
    }
    while (!(bits & 0x80000000u)) {
        bits <<= 1;
        indEntry++;
    }
    e = HEAD(g, indEntry)->pNext;
    sizeLeft = e->sizeFront - sizeEntry;
    indLeft = SIZE_INDEX(sizeLeft);

    if (indLeft != indEntry) {
        /* Off its list; what remains goes on the list for its size. */
        if (e->pNext == e->pPrev)
            sbh_clear_bits(h, r, indGroup, indEntry);
        sbh_unlink(e);
        if (sizeLeft != 0)
            sbh_link_head(h, r, indGroup, g, indLeft, e);
    }
    if (sizeLeft != 0) {
        e->sizeFront = sizeLeft;
        *(int32_t *)((uint8_t *)e + sizeLeft - 4) = sizeLeft;
    }
    alloc = ENTRY_AT(e, sizeLeft);
    alloc->sizeFront = sizeEntry + 1;
    *(int32_t *)((uint8_t *)alloc + sizeEntry - 4) = sizeEntry + 1;
    if (g->cntEntries++ == 0 && h == crt_pHeaderDefer && indGroup == crt_indGroupDefer)
        crt_pHeaderDefer = NULL;
    r->indGroupUse = indGroup;
    return (uint8_t *)alloc + 4;
}

void __cdecl crt_sbh_free_block(CRT_SBH_HEADER *h, void *p)
{
    CRT_SBH_REGION *r = h->pRegion;
    int32_t         indGroup = (int32_t)(((uint8_t *)p - h->pHeapData) >> 15);
    CRT_SBH_GROUP  *g = &r->grpHeadList[indGroup];
    CRT_SBH_ENTRY  *e = ENTRY_AT(p, -4), *next;
    int32_t         sizeEntry = e->sizeFront - 1;
    int32_t         sizePrev = *(int32_t *)((uint8_t *)e - 4);
    int32_t         sizeNext, indEntry, indPrev = -1;
    int32_t         prevFree = !(sizePrev & 1);

    next = ENTRY_AT(e, sizeEntry);
    sizeNext = next->sizeFront;
    if (!(sizeNext & 1)) {
        /* The block after is free: absorb it. */
        int32_t indNext = SIZE_INDEX(sizeNext);

        if (next->pNext == next->pPrev)
            sbh_clear_bits(h, r, indGroup, indNext);
        sbh_unlink(next);
        sizeEntry += sizeNext;
    }
    indEntry = SIZE_INDEX(sizeEntry);
    if (prevFree) {
        /* The block before is free: it absorbs this one, and moves list
         * only if its class changes. */
        e = ENTRY_AT(e, -sizePrev);
        indPrev = SIZE_INDEX(sizePrev);
        sizeEntry += sizePrev;
        indEntry = SIZE_INDEX(sizeEntry);
        if (indPrev != indEntry) {
            if (e->pNext == e->pPrev)
                sbh_clear_bits(h, r, indGroup, indPrev);
            sbh_unlink(e);
        }
    }
    if (!prevFree || indPrev != indEntry)
        sbh_link_head(h, r, indGroup, g, indEntry, e);
    e->sizeFront = sizeEntry;
    *(int32_t *)((uint8_t *)e + sizeEntry - 4) = sizeEntry;

    if (--g->cntEntries == 0) {
        /* The group is empty. The one emptied before it is decommitted
         * now, and this one waits its turn -- so a group that fills again
         * at once costs nothing. */
        CRT_SBH_HEADER *d = crt_pHeaderDefer;

        if (d != NULL) {
            int32_t         indD = crt_indGroupDefer;
            CRT_SBH_REGION *rd = d->pRegion;

            VirtualFree(d->pHeapData + (indD << 15), SBH_GROUP_BYTES, MEM_DECOMMIT);
            d->bitvCommit |= 0x80000000u >> indD;
            rd->bitvGroupLo[indD] = 0;
            if (--rd->cntRegionSize[63] == 0)
                d->bitvEntryLo &= ~1u;
            if (d->bitvCommit == 0xFFFFFFFFu) {
                /* Nothing committed in the region: it goes, and the list
                 * closes over it. */
                CRT_SBH_HEADER *listEnd = crt_pHeaderList + crt_cntHeaderList;

                VirtualFree(d->pHeapData, 0, MEM_RELEASE);
                HeapFree(crt_crtheap, 0, rd);
                crt_memmove(d, d + 1, (uint32_t)((uint8_t *)listEnd - (uint8_t *)(d + 1)));
                crt_cntHeaderList--;
                if (h > d)
                    h--;
                crt_pHeaderScan = crt_pHeaderList;
            }
        }
        crt_pHeaderDefer = h;
        crt_indGroupDefer = indGroup;
    }
}

int32_t __cdecl crt_sbh_resize_block(CRT_SBH_HEADER *h, void *p, uint32_t n)
{
    CRT_SBH_REGION *r = h->pRegion;
    int32_t         indGroup = (int32_t)(((uint8_t *)p - h->pHeapData) >> 15);
    CRT_SBH_GROUP  *g = &r->grpHeadList[indGroup];
    int32_t         sizeNew = (int32_t)((n + 0x17) & ~0xFu);
    int32_t         sizeEntry = *(int32_t *)((uint8_t *)p - 4) - 1;
    CRT_SBH_ENTRY  *next = ENTRY_AT(p, sizeEntry - 4);
    int32_t         sizeNext = next->sizeFront;

    if (sizeNew > sizeEntry) {
        /* Growing: only into a free block behind, and only whole. */
        int32_t sizeLeft;

        if (sizeNext & 1)
            return 0;
        if (sizeNew > sizeEntry + sizeNext)
            return 0;
        if (next->pNext == next->pPrev)
            sbh_clear_bits(h, r, indGroup, SIZE_INDEX(sizeNext));
        sbh_unlink(next);
        sizeLeft = sizeNext - (sizeNew - sizeEntry);
        if (sizeLeft > 0) {
            CRT_SBH_ENTRY *left = ENTRY_AT(p, sizeNew - 4);

            sbh_link_head(h, r, indGroup, g, SIZE_INDEX(sizeLeft), left);
            left->sizeFront = sizeLeft;
            *(int32_t *)((uint8_t *)left + sizeLeft - 4) = sizeLeft;
        }
        *(int32_t *)((uint8_t *)p - 4) = sizeNew + 1;
        *(int32_t *)((uint8_t *)p + sizeNew - 8) = sizeNew + 1;
        return 1;
    }
    if (sizeNew < sizeEntry) {
        /* Shrinking: the remainder becomes a free block, merged with a
         * free one behind it. */
        CRT_SBH_ENTRY *rem = ENTRY_AT(p, sizeNew - 4);
        int32_t        sizeRem = sizeEntry - sizeNew;

        *(int32_t *)((uint8_t *)p - 4) = sizeNew + 1;
        *(int32_t *)((uint8_t *)rem - 4) = sizeNew + 1;
        if (!(sizeNext & 1)) {
            if (next->pNext == next->pPrev)
                sbh_clear_bits(h, r, indGroup, SIZE_INDEX(sizeNext));
            sbh_unlink(next);
            sizeRem += sizeNext;
        }
        sbh_link_head(h, r, indGroup, g, SIZE_INDEX(sizeRem), rem);
        rem->sizeFront = sizeRem;
        *(int32_t *)((uint8_t *)rem + sizeRem - 4) = sizeRem;
    }
    return 1;
}

int32_t __cdecl crt_callnewh(uint32_t n)
{
    int32_t (__cdecl *handler)(uint32_t) = crt_pnhheap;

    if (!handler)
        return 0;
    return handler(n) != 0;
}

void *__cdecl crt_heap_alloc(uint32_t n)
{
    uint32_t rounded;

    ensure_heap();
    if (n <= crt_sbh_threshold) {
        void *p = crt_sbh_alloc_block(n);

        if (p)
            return p;
    }
    rounded = ((n ? n : 1) + 0xF) & ~0xFu;
    return HeapAlloc(crt_crtheap, 0, rounded);
}

/* AM2_TRACE_HEAP=1: one line per allocation, free and realloc, with the
 * pump number, so two games' heaps can be compared call for call. The
 * hybrid installs these three over the original's entries under the same
 * switch (src/hybrid/loader.cpp), so both sides log the same code. */
static int32_t heap_trace = -1;
static const void *heap_site;   /* the game's call site, from the entry that took it */
#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
extern "C" uint32_t am2_host_pump_number(void);
#define HEAP_TID  ((unsigned)syscall(SYS_gettid))
#define HEAP_PUMP ((unsigned)am2_host_pump_number())
#else                         /* the mingw selftest: no platform, no pumps */
#define HEAP_TID  0u
#define HEAP_PUMP 0u
#endif

static inline int32_t heap_tracing(void)
{
    if (heap_trace < 0)
        heap_trace = getenv("AM2_TRACE_HEAP") != NULL;
    return heap_trace;
}

static void *nh_malloc_body(uint32_t n, int32_t nhflag)
{
    if (n > 0xFFFFFFE0u)
        return NULL;
    for (;;) {
        void *p = crt_heap_alloc(n);

        if (p)
            return p;
        if (nhflag == 0)
            return NULL;
        if (!crt_callnewh(n))
            return NULL;
    }
}

void *__cdecl crt_nh_malloc(uint32_t n, int32_t nhflag)
{
    void *p = nh_malloc_body(n, nhflag);

    if (heap_tracing()) {
        fprintf(stderr, "HEAP pump %u tid %u alloc %u -> %p site %p\n", HEAP_PUMP, HEAP_TID,
                (unsigned)n, p, heap_site);
        heap_site = NULL;
    }
    return p;
}

void *__cdecl crt_malloc(uint32_t n)
{
    heap_site = __builtin_return_address(0);
    return crt_nh_malloc(n, crt_newmode);
}

void *__cdecl crt_operator_new(uint32_t n)
{
    heap_site = __builtin_return_address(0);
    return crt_nh_malloc(n, 1);
}

void __cdecl crt_free(void *p)
{
    CRT_SBH_HEADER *h;

    if (heap_tracing())
        fprintf(stderr, "HEAP pump %u tid %u free %p site %p\n", HEAP_PUMP, HEAP_TID, p,
                __builtin_return_address(0));
    if (!p)
        return;
    ensure_heap();
    h = crt_sbh_find_block(p);
    if (h)
        crt_sbh_free_block(h, p);
    else
        HeapFree(crt_crtheap, 0, p);
}

void __cdecl crt_operator_delete(void *p)
{
    crt_free(p);
}

uint32_t __cdecl crt_msize(const void *p)
{
    ensure_heap();
    if (crt_sbh_find_block(p))
        return (uint32_t)(*(const int32_t *)((const uint8_t *)p - 4) - 9);
    return (uint32_t)HeapSize(crt_crtheap, 0, p);
}

void *__cdecl crt_calloc(uint32_t count, uint32_t size)
{
    uint32_t total = count * size;
    uint32_t rounded = total;
    void    *p;

    ensure_heap();
    if (total <= 0xFFFFFFE0u)
        rounded = ((total ? total : 1) + 0xF) & ~0xFu;
    for (;;) {
        p = NULL;
        if (rounded <= 0xFFFFFFE0u) {
            if (total <= crt_sbh_threshold) {
                p = crt_sbh_alloc_block(total);
                if (p) {
                    uint8_t *q = (uint8_t *)p;
                    uint32_t i;

                    for (i = 0; i < total; i++)
                        q[i] = 0;
                    return p;
                }
            }
            p = HeapAlloc(crt_crtheap, HEAP_ZERO_MEMORY, rounded);
            if (p)
                return p;
        }
        if (crt_newmode == 0)
            return p;
        if (!crt_callnewh(rounded))
            return NULL;
    }
}

static void *realloc_body(void *p, uint32_t n)
{
    CRT_SBH_HEADER *h;
    uint32_t        rounded = 0;

    if (!p)
        return crt_malloc(n);
    if (n == 0) {
        crt_free(p);
        return NULL;
    }
    ensure_heap();
    for (;;) {
        void *q = NULL;

        if (n <= 0xFFFFFFE0u) {
            h = crt_sbh_find_block(p);
            if (h) {
                if (n <= crt_sbh_threshold) {
                    if (crt_sbh_resize_block(h, p, n))
                        return p;
                    q = crt_sbh_alloc_block(n);
                    if (q) {
                        uint32_t old = (uint32_t)(*(int32_t *)((uint8_t *)p - 4) - 1);

                        crt_memcpy(q, p, old < n ? old : n);
                        crt_sbh_free_block(h, p);
                        return q;
                    }
                }
                /* Out of the small-block heap into the big one. */
                rounded = ((n ? n : 1) + 0xF) & ~0xFu;
                q = HeapAlloc(crt_crtheap, 0, rounded);
                if (q) {
                    uint32_t old = (uint32_t)(*(int32_t *)((uint8_t *)p - 4) - 1);

                    crt_memcpy(q, p, old < rounded ? old : rounded);
                    crt_sbh_free_block(h, p);
                    return q;
                }
            } else {
                rounded = ((n ? n : 1) + 0xF) & ~0xFu;
                q = HeapReAlloc(crt_crtheap, 0, p, rounded);
                if (q)
                    return q;
            }
        }
        if (crt_newmode == 0)
            return q;
        if (!crt_callnewh(rounded))
            return NULL;
    }
}

void *__cdecl crt_realloc(void *p, uint32_t n)
{
    void *q = realloc_body(p, n);

    if (heap_tracing())
        fprintf(stderr, "HEAP pump %u tid %u realloc %p %u -> %p site %p\n", HEAP_PUMP, HEAP_TID, p,
                (unsigned)n, q, __builtin_return_address(0));
    return q;
}
