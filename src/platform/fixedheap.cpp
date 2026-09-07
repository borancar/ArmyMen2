/* fixedheap.cpp -- the platform's deterministic heap: every HeapAlloc and
 * every VirtualAlloc reservation lands at an address that depends only on
 * the sequence of calls before it, so two processes running the same game
 * over this platform hold their objects at the SAME addresses.
 *
 * WHY THE ADDRESSES MATTER. The game's depth comparator (0x0041D740) breaks
 * a tie between two objects at the same place by comparing their POINTERS,
 * so the order two sandbags are drawn in -- and which one's pixel wins
 * where they overlap -- is decided by where malloc put them. That is the
 * original's behaviour and is faithfully reproduced; what it means is that
 * a comparison of the original against the reconstruction is exact only if
 * both allocate identically. The CRT's small-block heap is reconstructed
 * and deterministic already; below it, HeapAlloc went to glibc and
 * VirtualAlloc to mmap(NULL), and both hand out whatever the process
 * happens to have free. The first live corner pixel the side-by-side could
 * not explain was two fence segments drawn in opposite orders for exactly
 * this reason.
 *
 * TWO REGIONS, BOTH FIXED. Blocks (HeapAlloc) come from a first-fit heap
 * over 96 MB at 0x0C000000 -- the development build's old arena, moved here
 * so the hybrid has it too and clear of the randomised brk heap -- and reservations (VirtualAlloc MEM_RESERVE,
 * the small-block heap's megabytes) are 1 MB slots over 64 MB at
 * 0x12000000. Both are mapped with MAP_FIXED_NOREPLACE and MAP_NORESERVE,
 * so the kernel commits pages as they are touched; a Boot Camp mission
 * uses about 14 MB of the first.
 *
 * A block is zero-filled when handed out. Windows does not promise that for
 * HeapAlloc and glibc does not either, but every allocator this game has
 * run on gave it zero pages for large blocks, and the campaign's -dbg path
 * reads a block it never wrote: with stale contents it came up as a black
 * screen. Zero is what both games see, which is what matters here.
 *
 * AM2_FIXED_HEAP=0 turns it off (glibc and mmap again, the pre-2026-09-07
 * behaviour), which is the bisecting switch. The development switches the
 * arena had keep working: AM2_DEV_NOREUSE, AM2_DEV_POISON, AM2_DEV_CANARY,
 * and the per-frame canary scan the dev tools call.
 *
 * The savestate (src/standalone/devtools.cpp) snapshots both regions
 * through the accessors at the bottom, so a `snap` now carries the CRT's
 * small-block heap as well as the big blocks. */
#include "platform.h"

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* NOT 0x0A000000, where the development arena used to be. An i386 ELF's
 * brk heap starts up to 32 MB past its bss under ASLR -- the hybrid's has
 * been seen at 0x0903D000 -- and MAP_FIXED_NOREPLACE then fails with
 * EEXIST on the run where it lands high, which is about one run in
 * thirty and reads as a broken build. 0x0C000000 is past the highest brk
 * base either binary can draw, and its high nibble is still neither 2
 * nor 8, which is how the game tells a uid from a pointer. */
#define HEAP_BASE   ((uint8_t *)0x0C000000u)
#define HEAP_SIZE   (96u << 20)
#define HEAP_ALIGN  16u
#define BLOCK_MAGIC 0x41524E41u   /* 'ARNA' */

#define RESV_BASE   ((uint8_t *)0x12000000u)
#define RESV_SLOT   (1u << 20)
#define RESV_SLOTS  64u

/* A block: header, payload. Free blocks are threaded through `next` (which
 * overlays the payload's first word) on a single first-fit list, in address
 * order so neighbours coalesce. Everything is inside the region, so a
 * snapshot of the region is a snapshot of the allocator. */
typedef struct Block {
    uint32_t size;            /* payload bytes, a multiple of the alignment */
    uint32_t magic;
    uint32_t tag;             /* index into the canary side table, or 0 */
    uint32_t pad;
    struct Block *next;       /* free list, valid only when free */
} Block;
#define HDR 16u

typedef struct HeapHead {
    uint32_t used;            /* bytes handed out from the base, high-water */
    uint32_t reserved;
    Block   *free_list;
    uint32_t allocs, frees;
    uint8_t  pad[64 - 24];
} HeapHead;

#define HEAD  ((HeapHead *)HEAP_BASE)
#define FIRST (HEAP_BASE + sizeof(HeapHead))

typedef struct Canary {
    uint32_t off, size, pc, freed, reported;
} Canary;
#define CANARY_MAX (1u << 20)

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int32_t         state = -1;       /* -1 untried, 0 off, 1 on */
static int32_t         noreuse, poison, canary;
static Canary         *canaries;
static uint32_t        canary_count;
static uint8_t         slot_used[RESV_SLOTS];
/* The number of slots in the reservation that STARTS at this slot; 0 for a
 * free slot or an interior slot of a multi-slot reservation. Release frees
 * exactly this many, so two reservations that happen to be adjacent -- with
 * no free slot between them -- are not run together into one. Without it,
 * releasing one reservation madvise-zeroed the live one beside it, and a
 * later free of a block in that zeroed region walked a null free-list link.
 * That is the whole grenade-recording heap crash. */
static uint8_t         slot_run[RESV_SLOTS];

static void heap_init(void)
{
    void *p;

    p = mmap(HEAP_BASE, HEAP_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != HEAP_BASE) {
        fprintf(stderr, "fixedheap: cannot map the heap at %p: %s\n", (void *)HEAP_BASE, strerror(errno));
        abort();
    }
    p = mmap(RESV_BASE, RESV_SLOT * RESV_SLOTS, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != RESV_BASE) {
        fprintf(stderr, "fixedheap: cannot map the reservations at %p: %s\n", (void *)RESV_BASE, strerror(errno));
        abort();
    }
    memset(HEAD, 0, sizeof *HEAD);
    HEAD->used = (uint32_t)sizeof(HeapHead);
    noreuse = getenv("AM2_DEV_NOREUSE") != NULL;
    poison  = getenv("AM2_DEV_POISON") != NULL;
    canary  = getenv("AM2_DEV_CANARY") != NULL;
    if (canary) {
        noreuse = poison = 1;
        canaries = (Canary *)mmap(NULL, CANARY_MAX * sizeof(Canary), PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        canary_count = 1;
    }
}

int32_t am2_fixed_heap_on(void)
{
    if (state < 0) {
        const char *e = getenv("AM2_FIXED_HEAP");
        state = !(e && *e == '0');
        if (state)
            heap_init();
    }
    return state;
}

static void note_alloc(Block *b, uint32_t pc)
{
    if (!canary || canary_count >= CANARY_MAX) {
        b->tag = 0;
        return;
    }
    b->tag = canary_count++;
    canaries[b->tag].off  = (uint32_t)((uint8_t *)b - HEAP_BASE);
    canaries[b->tag].size = b->size;
    canaries[b->tag].pc   = pc;
    canaries[b->tag].freed = 0;
    canaries[b->tag].reported = 0;
}

static inline uint32_t round_up(uint32_t n)
{
    return (n + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
}

static void *heap_alloc(uint32_t n, uint32_t pc)
{
    Block  **pp, *b;
    uint32_t want = round_up(n ? n : 1);

    for (pp = &HEAD->free_list; (b = *pp) != NULL; pp = &b->next) {
        if (b->size < want)
            continue;
        if (b->size >= want + sizeof(Block) + HEAP_ALIGN) {
            Block *rest = (Block *)((uint8_t *)b + HDR + want);
            rest->size  = b->size - want - HDR;
            rest->magic = 0;
            rest->next  = b->next;
            b->size     = want;
            *pp = rest;
        } else {
            *pp = b->next;
        }
        b->magic = BLOCK_MAGIC;
        HEAD->allocs++;
        note_alloc(b, pc);
        memset((uint8_t *)b + HDR, 0, b->size);
        return (uint8_t *)b + HDR;
    }
    if (HEAD->used + HDR + want > HEAP_SIZE)
        return NULL;
    b = (Block *)(HEAP_BASE + HEAD->used);
    b->size  = want;
    b->magic = BLOCK_MAGIC;
    HEAD->used += HDR + want;
    HEAD->allocs++;
    note_alloc(b, pc);
    return (uint8_t *)b + HDR;
}

int32_t am2_fixed_owns(const void *p)
{
    return (const uint8_t *)p >= FIRST && (const uint8_t *)p < HEAP_BASE + HEAP_SIZE;
}

static void heap_free(void *p)
{
    Block  *b = (Block *)((uint8_t *)p - HDR);
    Block **pp;

    if (b->magic != BLOCK_MAGIC) {
        fprintf(stderr, "fixedheap: free of a block with no header at %p\n", p);
        return;
    }
    b->magic = 0;
    HEAD->frees++;
    if (canary && b->tag && b->tag < canary_count)
        canaries[b->tag].freed = HEAD->frees;
    if (poison)
        memset((uint8_t *)b + HDR, 0xDD, b->size);
    if (noreuse)
        return;
    for (pp = &HEAD->free_list; *pp && *pp < b; pp = &(*pp)->next)
        ;
    b->next = *pp;
    *pp = b;
    if (b->next && (uint8_t *)b + HDR + b->size == (uint8_t *)b->next) {
        b->size += HDR + b->next->size;
        b->next  = b->next->next;
    }
    if (pp != &HEAD->free_list) {
        Block *prev = (Block *)((uint8_t *)pp - offsetof(Block, next));
        if ((uint8_t *)prev + HDR + prev->size == (uint8_t *)b) {
            prev->size += HDR + b->size;
            prev->next  = b->next;
        }
    }
}

void *am2_fixed_alloc(size_t n, uint32_t pc)
{
    void *p;
    pthread_mutex_lock(&lock);
    p = heap_alloc((uint32_t)n, pc);
    pthread_mutex_unlock(&lock);
    return p;
}

void am2_fixed_free(void *p)
{
    if (!p || !am2_fixed_owns(p))
        return;
    pthread_mutex_lock(&lock);
    heap_free(p);
    pthread_mutex_unlock(&lock);
}

size_t am2_fixed_size(const void *p)
{
    return am2_fixed_owns(p) ? ((const Block *)((const uint8_t *)p - HDR))->size : 0;
}

void *am2_fixed_realloc(void *p, size_t n, uint32_t pc)
{
    void    *q;
    uint32_t old;

    if (!p)
        return am2_fixed_alloc(n, pc);
    if (n == 0) {
        am2_fixed_free(p);
        return NULL;
    }
    old = (uint32_t)am2_fixed_size(p);
    if (old >= n)
        return p;
    q = am2_fixed_alloc(n, pc);
    if (!q)
        return NULL;
    memcpy(q, p, old);
    am2_fixed_free(p);
    return q;
}

/* ---- reservations --------------------------------------------------------- */

void *am2_fixed_reserve(size_t size)
{
    uint32_t need = (uint32_t)((size + RESV_SLOT - 1) / RESV_SLOT), i, j;

    pthread_mutex_lock(&lock);
    for (i = 0; i + need <= RESV_SLOTS; i++) {
        for (j = 0; j < need && !slot_used[i + j]; j++)
            ;
        if (j == need) {
            for (j = 0; j < need; j++)
                slot_used[i + j] = 1;
            slot_run[i] = (uint8_t)need;
            pthread_mutex_unlock(&lock);
            return RESV_BASE + (size_t)i * RESV_SLOT;
        }
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}

int32_t am2_fixed_release(void *addr)
{
    uint32_t i, first;

    if ((uint8_t *)addr < RESV_BASE || (uint8_t *)addr >= RESV_BASE + RESV_SLOT * RESV_SLOTS)
        return 0;
    first = (uint32_t)(((uint8_t *)addr - RESV_BASE) / RESV_SLOT);
    pthread_mutex_lock(&lock);
    /* Free EXACTLY this reservation's slots -- not every used slot up to the
     * next gap, which would swallow an adjacent reservation. */
    {
        uint32_t n = slot_run[first];

        if (n == 0) {             /* not a reservation start */
            pthread_mutex_unlock(&lock);
            return 0;
        }
        slot_run[first] = 0;
        for (i = first; i < first + n; i++)
            slot_used[i] = 0;
    }
    /* The pages read as zero again afterwards, as a fresh reservation's
     * would. */
    madvise(RESV_BASE + (size_t)first * RESV_SLOT, (size_t)(i - first) * RESV_SLOT, MADV_DONTNEED);
    pthread_mutex_unlock(&lock);
    return 1;
}

int32_t am2_fixed_is_reservation(const void *addr)
{
    return (const uint8_t *)addr >= RESV_BASE && (const uint8_t *)addr < RESV_BASE + RESV_SLOT * RESV_SLOTS;
}

/* ---- for the savestate and the frame hook ---------------------------------- */

uint8_t *am2_fixed_heap_base(void)  { return HEAP_BASE; }
uint32_t am2_fixed_heap_used(void)  { return am2_fixed_heap_on() ? HEAD->used : 0; }
uint32_t am2_fixed_heap_limit(void) { return HEAP_SIZE; }
uint8_t *am2_fixed_resv_base(void)  { return RESV_BASE; }

uint32_t am2_fixed_resv_used(void)
{
    uint32_t i, top = 0;
    for (i = 0; i < RESV_SLOTS; i++)
        if (slot_used[i])
            top = i + 1;
    return top * RESV_SLOT;
}

void am2_fixed_resv_mark(uint32_t used)
{
    uint32_t i;
    for (i = 0; i < RESV_SLOTS; i++)
        slot_used[i] = (i * RESV_SLOT < used);
}

void am2_fixed_stats(uint32_t *allocs, uint32_t *frees, void **free_head)
{
    *allocs = HEAD->allocs;
    *frees = HEAD->frees;
    *free_head = HEAD->free_list;
}

/* Every frame under AM2_DEV_CANARY: has any freed block lost its poison? */
void am2_fixed_frame_check(void)
{
    uint32_t i, shown = 0;

    if (!canary)
        return;
    for (i = 1; i < canary_count && shown < 4; i++) {
        Canary        *c = &canaries[i];
        const uint8_t *p = HEAP_BASE + c->off + HDR;
        uint32_t       k;

        if (!c->freed || c->reported)
            continue;
        for (k = 0; k < c->size; k++) {
            if (p[k] != 0xDD) {
                fprintf(stderr, "fixedheap: FREED BLOCK WRITTEN: tag %u, %u bytes at %p allocated by "
                        "0x%08x, freed as #%u; first bad byte at +%u = %02x (dword %08x)\n",
                        i, c->size, (const void *)p, c->pc, c->freed, k, p[k],
                        *(const uint32_t *)(p + (k & ~3u)));
                c->reported = 1;
                shown++;
                break;
            }
        }
    }
}
