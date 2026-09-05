/* devtools.cpp -- what the DEVELOPMENT binary has and the player's does not.
 *
 * `make native-dev` builds build/armymen2-dev with AM2_DEVTOOLS defined; this
 * file is empty otherwise, so the release binary carries none of it. Three
 * things live here:
 *
 *   THE ARENA. Every allocation the game makes -- its malloc/realloc/free,
 *   its operator new/delete, and the CRT entries standalone.h remaps -- comes
 *   from one region mapped at a FIXED address, with a deterministic first-fit
 *   allocator whose bookkeeping lives inside the region. That is what makes a
 *   savestate possible: the game's globals (the carried .data/.bss span at
 *   0x0046F000..0x00666000) are full of pointers into its heap, and a heap
 *   that is always at the same address, laid out by the same sequence of
 *   calls, is one that can be written out and read back as bytes.
 *
 *   SAVESTATES. `snap save FILE` and `snap load FILE` on the control socket
 *   (FILE absolute -- the game chdirs) write and restore the span and the
 *   arena. F5 and F9 are NOT these: they are the game's own save and load,
 *   the portable ones -- see quick_game_save below. They run on the GAME THREAD, between frames, through the
 *   host's frame hook -- a restore that landed in the middle of a frame would
 *   put the new globals under the old frame's locals.
 *
 *   WHAT A SAVESTATE DOES NOT HOLD, said plainly. Anything the platform layer
 *   owns: DirectDraw surfaces and their pixels, DirectSound buffers, open
 *   files, the window. The game's globals point at those objects and the
 *   restored pointers are only good while the same objects exist -- so a
 *   state is valid within the SESSION and MISSION it was taken in. Loading a
 *   state from another mission, or after the sprites it references were
 *   released, is undefined. This is a development tool, not a save game;
 *   the game's own SAVE GAME is the portable one.
 */
#ifdef AM2_DEVTOOLS

#include "../inject/win32.h"
#include "../platform/platform.h"
#include "../inject/hooklog.h"
#include <stddef.h>
#include <sys/mman.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>

/* ---- the arena ---------------------------------------------------------- */

/* WHERE IT SITS IS NOT A FREE CHOICE. The game overloads fields with a uid
 * OR a pointer and tells them apart by value: a uid carries its kind in the
 * high nibble (objdump shows 0x200003E8, 0x800003E9), so a pointer whose
 * high nibble is non-zero reads as a uid. The MSVC heap under Windows and
 * glibc's brk heap here both hand out 0x00xxxxxx..0x0Fxxxxxx addresses and
 * the game has never seen anything else; an arena at 0x20000000 turned the
 * campaign mission into a black screen with a different sub-state. So the
 * arena lives in the low 256 MB, above everything the loader places (the
 * game's span ends at 0x00666000, our text is at 0x00700000, its data and
 * glibc's brk heap follow, and the ELF's startup stub sits at the
 * traditional 0x08048000) and below 0x10000000. 96 MB reserved; the kernel
 * commits pages as they are touched, and a Boot Camp mission uses 14. */
#define AM2_ARENA_BASE  ((uint8_t *)0x0A000000u)
#define AM2_ARENA_SIZE  (96u << 20)
#define AM2_ARENA_ALIGN 16u
#define AM2_BLOCK_MAGIC 0x41524E41u   /* 'ARNA' */

/* A block: header, payload. Free blocks are threaded through `next` (which
 * overlays the payload's first word) on a single first-fit list, in address
 * order so neighbours coalesce. Everything here is inside the arena, so a
 * snapshot of the arena is a snapshot of the allocator. */
typedef struct AM2_Block {
    uint32_t size;            /* payload bytes, a multiple of the alignment */
    uint32_t magic;
    uint32_t tag;             /* index into the canary side table, or 0 */
    uint32_t pad;
    struct AM2_Block *next;   /* free list, valid only when free */
} AM2_Block;
#define HDR 16u               /* the four words above the payload */

typedef struct AM2_ArenaHead {
    uint32_t  used;           /* bytes handed out from the base, high-water */
    uint32_t  reserved;
    AM2_Block *free_list;
    uint32_t  allocs, frees;  /* counters, for `state info` */
    uint8_t   pad[64 - 24];
} AM2_ArenaHead;

#define ARENA_HEAD  ((AM2_ArenaHead *)AM2_ARENA_BASE)
#define ARENA_FIRST (AM2_ARENA_BASE + sizeof(AM2_ArenaHead))

static pthread_mutex_t am2_arena_lock = PTHREAD_MUTEX_INITIALIZER;
static int32_t         am2_arena_ready;
/* Three bisecting switches. AM2_DEV_NOREUSE=1: a freed block is never handed
 * out again and its contents are left alone, so a read of freed memory
 * sees what it saw before. AM2_DEV_POISON=1: a freed block's payload is
 * filled with 0xDD, so such a read sees garbage at once. AM2_DEV_CANARY=1
 * implies both and, every frame, checks that every freed block still holds
 * its poison -- a block that does not was WRITTEN after its free, and the
 * side table says how big it was and which call site allocated it. */
static int32_t         am2_arena_noreuse, am2_arena_poison, am2_arena_canary;

typedef struct AM2_Canary {
    uint32_t off;             /* block start, from the arena base */
    uint32_t size;
    uint32_t pc;              /* the allocating call site */
    uint32_t freed;           /* free ordinal, 0 while live */
    uint32_t reported;
} AM2_Canary;
#define AM2_CANARY_MAX (1u << 20)
static AM2_Canary *am2_canaries;
static uint32_t    am2_canary_count;

static void arena_init(void)
{
    void *p;

    if (am2_arena_ready)
        return;
    p = mmap(AM2_ARENA_BASE, AM2_ARENA_SIZE, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED_NOREPLACE, -1, 0);
    if (p != AM2_ARENA_BASE) {
        fprintf(stderr, "devtools: cannot map the arena at %p: %s\n",
                (void *)AM2_ARENA_BASE, strerror(errno));
        abort();
    }
    memset(ARENA_HEAD, 0, sizeof *ARENA_HEAD);
    ARENA_HEAD->used = (uint32_t)sizeof(AM2_ArenaHead);
    am2_arena_noreuse = getenv("AM2_DEV_NOREUSE") != NULL;
    am2_arena_poison  = getenv("AM2_DEV_POISON") != NULL;
    am2_arena_canary  = getenv("AM2_DEV_CANARY") != NULL;
    if (am2_arena_canary) {
        am2_arena_noreuse = am2_arena_poison = 1;
        am2_canaries = (AM2_Canary *)mmap(NULL, AM2_CANARY_MAX * sizeof(AM2_Canary),
                                          PROT_READ | PROT_WRITE,
                                          MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
        am2_canary_count = 1;         /* tag 0 means "none" */
    }
    am2_arena_ready = 1;
}

static void canary_note_alloc(AM2_Block *b, uint32_t pc)
{
    if (!am2_arena_canary || am2_canary_count >= AM2_CANARY_MAX) {
        b->tag = 0;
        return;
    }
    b->tag = am2_canary_count++;
    am2_canaries[b->tag].off  = (uint32_t)((uint8_t *)b - AM2_ARENA_BASE);
    am2_canaries[b->tag].size = b->size;
    am2_canaries[b->tag].pc   = pc;
    am2_canaries[b->tag].freed = 0;
    am2_canaries[b->tag].reported = 0;
}

/* Every frame under AM2_DEV_CANARY: has any freed block lost its poison? */
static void canary_scan(void)
{
    uint32_t i, shown = 0;

    if (!am2_arena_canary)
        return;
    for (i = 1; i < am2_canary_count && shown < 4; i++) {
        AM2_Canary    *c = &am2_canaries[i];
        const uint8_t *p = AM2_ARENA_BASE + c->off + HDR;
        uint32_t       k;

        if (!c->freed || c->reported)
            continue;
        for (k = 0; k < c->size; k++) {
            if (p[k] != 0xDD) {
                hooklog("devtools: FREED BLOCK WRITTEN: tag %u, %u bytes at %p allocated by "
                        "0x%08x, freed as #%u; first bad byte at +%u = %02x (dword %08x), clock %u",
                        i, c->size, (const void *)p, c->pc, c->freed, k, p[k],
                        *(const uint32_t *)(p + (k & ~3u)), *(const uint32_t *)0x00511E04u);
                c->reported = 1;
                shown++;
                break;
            }
        }
    }
}

static inline uint32_t round_up(uint32_t n)
{
    return (n + AM2_ARENA_ALIGN - 1) & ~(AM2_ARENA_ALIGN - 1);
}

static void *arena_alloc(uint32_t n, uint32_t pc)
{
    AM2_Block **pp, *b;
    uint32_t    want = round_up(n ? n : 1);

    arena_init();
    /* First fit over the address-ordered free list; split what is left. */
    for (pp = &ARENA_HEAD->free_list; (b = *pp) != NULL; pp = &b->next) {
        if (b->size < want)
            continue;
        if (b->size >= want + sizeof(AM2_Block) + AM2_ARENA_ALIGN) {
            AM2_Block *rest = (AM2_Block *)((uint8_t *)b + HDR + want);

            rest->size  = b->size - want - HDR;
            rest->magic = 0;
            rest->next  = b->next;
            b->size     = want;
            *pp = rest;
        } else {
            *pp = b->next;
        }
        b->magic = AM2_BLOCK_MAGIC;
        ARENA_HEAD->allocs++;
        canary_note_alloc(b, pc);
        /* ZEROED, as a fresh page from glibc or from VirtualAlloc under the
         * MSVC CRT would be. The campaign's -dbg path reads something it
         * never wrote in a block this size: with libc's allocator that block
         * is a fresh mapping and reads as zero, and with stale contents here
         * the mission came up as a black screen. A zero fill is what every
         * allocator the game has run on gave it for large blocks, and it
         * also keeps a savestate free of whatever was there before. */
        memset((uint8_t *)b + HDR, 0, b->size);
        return (uint8_t *)b + HDR;
    }
    if (ARENA_HEAD->used + HDR + want > AM2_ARENA_SIZE)
        return NULL;
    b = (AM2_Block *)(AM2_ARENA_BASE + ARENA_HEAD->used);
    b->size  = want;
    b->magic = AM2_BLOCK_MAGIC;
    ARENA_HEAD->used += HDR + want;
    ARENA_HEAD->allocs++;
    canary_note_alloc(b, pc);
    return (uint8_t *)b + HDR;
}

static void arena_free(void *p)
{
    AM2_Block  *b = (AM2_Block *)((uint8_t *)p - HDR);
    AM2_Block **pp;

    if (!p)
        return;
    if ((uint8_t *)p < ARENA_FIRST || (uint8_t *)p >= AM2_ARENA_BASE + AM2_ARENA_SIZE) {
        /* Not ours: something allocated before the remap, or by libc. */
        free(p);
        return;
    }
    if (b->magic != AM2_BLOCK_MAGIC) {
        hooklog("devtools: free of a block with no header at %p", p);
        return;
    }
    b->magic = 0;
    ARENA_HEAD->frees++;
    if (am2_arena_canary && b->tag && b->tag < am2_canary_count)
        am2_canaries[b->tag].freed = ARENA_HEAD->frees;
    if (am2_arena_poison)
        memset((uint8_t *)b + HDR, 0xDD, b->size);
    if (am2_arena_noreuse)
        return;
    /* Insert in address order and coalesce with either neighbour. */
    for (pp = &ARENA_HEAD->free_list; *pp && *pp < b; pp = &(*pp)->next)
        ;
    b->next = *pp;
    *pp = b;
    if (b->next && (uint8_t *)b + HDR + b->size == (uint8_t *)b->next) {
        b->size += HDR + b->next->size;
        b->next  = b->next->next;
    }
    if (pp != &ARENA_HEAD->free_list) {
        AM2_Block *prev = (AM2_Block *)((uint8_t *)pp - offsetof(AM2_Block, next));

        if ((uint8_t *)prev + HDR + prev->size == (uint8_t *)b) {
            prev->size += HDR + b->size;
            prev->next  = b->next;
        }
    }
}

static void *dev_malloc_from(size_t n, uint32_t pc)
{
    void *p;

    pthread_mutex_lock(&am2_arena_lock);
    p = arena_alloc((uint32_t)n, pc);
    pthread_mutex_unlock(&am2_arena_lock);
    return p;
}

extern "C" void *am2_dev_malloc(size_t n)
{
    return dev_malloc_from(n, (uint32_t)(uintptr_t)__builtin_return_address(0));
}

extern "C" void am2_dev_free(void *p)
{
    pthread_mutex_lock(&am2_arena_lock);
    arena_free(p);
    pthread_mutex_unlock(&am2_arena_lock);
}

extern "C" void *am2_dev_realloc(void *p, size_t n)
{
    void    *q;
    uint32_t old;

    if (!p)
        return am2_dev_malloc(n);
    if (n == 0) {
        am2_dev_free(p);
        return NULL;
    }
    if ((uint8_t *)p < ARENA_FIRST || (uint8_t *)p >= AM2_ARENA_BASE + AM2_ARENA_SIZE)
        return realloc(p, n);       /* a libc block from before the remap */
    old = ((AM2_Block *)((uint8_t *)p - HDR))->size;
    if (old >= n)
        return p;
    q = dev_malloc_from(n, (uint32_t)(uintptr_t)__builtin_return_address(0));
    if (!q)
        return NULL;
    memcpy(q, p, old);
    am2_dev_free(p);
    return q;
}

/* The entries standalone.h hands the game's CRT seams: through crt.h's
 * pointers, so AM2_DEV_NOARENA=1 moves every allocation at once. */
#include "../game/crt.h"
extern "C" void *am2_dev_malloc_entry(size_t n)
{
    if (am2_malloc == am2_dev_malloc)
        return dev_malloc_from(n, (uint32_t)(uintptr_t)__builtin_return_address(0));
    return am2_malloc(n);
}
extern "C" void *am2_dev_realloc_entry(void *p, size_t n) { return am2_realloc(p, n); }
extern "C" void  am2_dev_free_entry(void *p) { am2_free(p); }

/* ---- savestates ----------------------------------------------------------- */

#define AM2_SPAN_LO   0x0046F000u
#define AM2_SPAN_HI   0x00666000u
#define AM2_STATE_MAGIC 0x32544153u   /* 'SAT2' */

typedef struct AM2_StateHeader {
    uint32_t magic;
    uint32_t span_lo, span_hi;
    uint32_t arena_base, arena_used;
    uint32_t game_clock;              /* ADDR_GAME_CLOCK_MS at the time, for the log */
} AM2_StateHeader;

static int32_t state_save(const char *path, char *out, size_t cap)
{
    AM2_StateHeader h;
    FILE           *fp = fopen(path, "wb");

    if (!fp) {
        snprintf(out, cap, "err cannot write %s: %s", path, strerror(errno));
        return 0;
    }
    arena_init();
    h.magic      = AM2_STATE_MAGIC;
    h.span_lo    = AM2_SPAN_LO;
    h.span_hi    = AM2_SPAN_HI;
    h.arena_base = (uint32_t)(uintptr_t)AM2_ARENA_BASE;
    h.arena_used = ARENA_HEAD->used;
    h.game_clock = *(const uint32_t *)0x00511E04u;
    if (fwrite(&h, sizeof h, 1, fp) != 1
        || fwrite((const void *)AM2_SPAN_LO, AM2_SPAN_HI - AM2_SPAN_LO, 1, fp) != 1
        || fwrite(AM2_ARENA_BASE, h.arena_used, 1, fp) != 1) {
        fclose(fp);
        snprintf(out, cap, "err short write to %s", path);
        return 0;
    }
    fclose(fp);
    snprintf(out, cap, "ok saved %s: span %u KB, arena %u KB, clock %u",
             path, (AM2_SPAN_HI - AM2_SPAN_LO) / 1024, h.arena_used / 1024,
             h.game_clock);
    return 1;
}

static int32_t state_load(const char *path, char *out, size_t cap)
{
    AM2_StateHeader h;
    FILE           *fp = fopen(path, "rb");
    uint32_t        before;

    if (!fp) {
        snprintf(out, cap, "err cannot read %s: %s", path, strerror(errno));
        return 0;
    }
    if (fread(&h, sizeof h, 1, fp) != 1 || h.magic != AM2_STATE_MAGIC
        || h.span_lo != AM2_SPAN_LO || h.span_hi != AM2_SPAN_HI
        || h.arena_base != (uint32_t)(uintptr_t)AM2_ARENA_BASE
        || h.arena_used > AM2_ARENA_SIZE) {
        fclose(fp);
        snprintf(out, cap, "err %s is not a savestate for this binary", path);
        return 0;
    }
    arena_init();
    before = ARENA_HEAD->used;
    if (fread((void *)AM2_SPAN_LO, AM2_SPAN_HI - AM2_SPAN_LO, 1, fp) != 1
        || fread(AM2_ARENA_BASE, h.arena_used, 1, fp) != 1) {
        fclose(fp);
        snprintf(out, cap, "err short read from %s -- the state is now half restored", path);
        return 0;
    }
    fclose(fp);
    /* Whatever the arena had grown to since is unreachable now; scrub it so
     * a stale pointer faults rather than finding old objects. */
    if (before > h.arena_used)
        memset(AM2_ARENA_BASE + h.arena_used, 0, before - h.arena_used);
    snprintf(out, cap, "ok loaded %s: arena %u KB, clock %u", path,
             h.arena_used / 1024, h.game_clock);
    return 1;
}

/* ---- running on the game thread -------------------------------------------- */

/* The socket thread parks a request here; the frame hook, which the host
 * calls from the game thread at the top of every pump, carries it out and
 * signals. One at a time is plenty. */
static pthread_mutex_t am2_req_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  am2_req_done = PTHREAD_COND_INITIALIZER;
static int32_t         am2_req_kind;      /* 0 none, 1 save, 2 load */
static char            am2_req_path[512];
static char            am2_req_reply[800];
static int32_t         am2_req_finished;
static int32_t         am2_hotkey;        /* 1 save, 2 load, set by the host pump */


/* F5 and F9 are the GAME'S save and load, not the raw savestate: a game
 * save is the file tools/enterlevel.sh can bring back in any build, in
 * any run, and that is what a fixture has to be. F5 calls SaveGame on the
 * game thread with the first free f5-N.sav under the folder the game-proc
 * block names (the player's, or `bootcamp`) and prints the path; F9 makes
 * the LOAD button's four writes for the latest one, and with
 * AM2_PAUSE_ON_ENTER the mission comes back frozen (SPACE releases it).
 * The raw savestates stay on the socket as `snap`, for a reload that does
 * not re-enter the level. */
#include "../game/gameproc.h"
#include "../game/crt.h"

static char am2_last_f5[64];

static void quick_game_save(void)
{
    const char *folder = (const char *)(uintptr_t)0x00511A68u;   /* ADDR_GAMEPROC_BLOCK */
    const char *base;
    char        cwd[512], name[32], probe[600];
    int32_t     n;

    if (*(const int32_t *)(uintptr_t)0x00511DA4u != 2) {
        fprintf(stderr, "devtools: F5 needs a mission\n");
        return;
    }
    /* Boot Camp has no player profile, so the folder string is empty and
     * SaveGame would refuse -- the menu never offers a save there. Give it
     * the level's own name, which sits 0x20 into the same block, so the
     * file lands in save\bootcamp and enterlevel.sh's default folder is
     * right. A campaign mission already carries the player's name. */
    if (!*folder) {
        const char *level = (const char *)(uintptr_t)0x00511A88u;  /* the level name */

        if (!*level) {
            fprintf(stderr, "devtools: F5: no folder and no level name to use for one\n");
            return;
        }
        strcpy((char *)folder, level);
        fprintf(stderr, "devtools: F5: no player folder; using the level's, %s\n", folder);
    }
    /* The save goes under the GAME directory -- SaveGame chdirs there
     * itself -- which is not the working directory, since the game wanders
     * through its data folders as it runs. AM2_GAMEDIR is what the native
     * build was started with. */
    base = getenv("AM2_GAMEDIR");
    if (!base || !*base)
        base = ".";
    cwd[0] = 0;
    am2_getcwd(cwd, sizeof cwd);
    for (n = 1; n < 1000; n++) {
        FILE *fp;

        snprintf(name, sizeof name, "f5-%d.sav", n);
        snprintf(probe, sizeof probe, "%s/save/%s/%s", base, folder, name);
        fp = fopen(probe, "rb");
        if (!fp)
            break;
        fclose(fp);
    }
    if (SaveGame(name)) {
        snprintf(am2_last_f5, sizeof am2_last_f5, "%s", name);
        fprintf(stderr, "devtools: F5 saved %s  (tools/enterlevel.sh -f %s \"%s\")\n",
                probe, folder, probe);
        hooklog("devtools: F5 saved save/%s/%s", folder, name);
    } else {
        fprintf(stderr, "devtools: F5: SaveGame refused %s\n", name);
    }
    /* SaveGame leaves the game chdir'd into save\folder; the game's own
     * callers go on to SetGameDir something else, so put the directory
     * back where it was. */
    if (cwd[0])
        am2_chdir(cwd);
}

static void quick_game_load(void)
{
    char *file = (char *)(uintptr_t)0x00511B88u;                  /* ADDR_GAMEPROC_STR_B */

    if (!am2_last_f5[0]) {
        fprintf(stderr, "devtools: F9 has nothing to load; press F5 in a mission first\n");
        return;
    }
    strcpy(file, am2_last_f5);
    *(int32_t *)(uintptr_t)0x00511DD8u = 1;                        /* ADDR_LOAD_PENDING */
    *(int32_t *)(uintptr_t)0x00511DB0u = 2;                        /* ADDR_STATE_WANTED */
    *(int32_t *)(uintptr_t)0x00511DACu = 1;                        /* ADDR_STATE_PENDING */
    fprintf(stderr, "devtools: F9 loading %s\n", am2_last_f5);
}

static void devtools_frame(void)
{
    int32_t kind = 0;

    canary_scan();

    pthread_mutex_lock(&am2_req_lock);
    if (am2_req_kind && !am2_req_finished)
        kind = am2_req_kind;
    pthread_mutex_unlock(&am2_req_lock);

    if (kind) {
        if (kind == 1)
            state_save(am2_req_path, am2_req_reply, sizeof am2_req_reply);
        else
            state_load(am2_req_path, am2_req_reply, sizeof am2_req_reply);
        hooklog("devtools: %s", am2_req_reply);
        pthread_mutex_lock(&am2_req_lock);
        am2_req_finished = 1;
        pthread_cond_broadcast(&am2_req_done);
        pthread_mutex_unlock(&am2_req_lock);
    }
    if (am2_hotkey) {
        int32_t which = am2_hotkey;

        am2_hotkey = 0;
        if (which == 1)
            quick_game_save();
        else
            quick_game_load();
    }
}

extern "C" void devtools_hotkey(int32_t which)
{
    am2_hotkey = which;
}

/* The control socket's `snap` command: snap save FILE | snap load FILE |
 * snap info. (`state` is the harness's own: what keys are held.) Answers 1 when it handled the line, with the reply in `out`. */
extern "C" int32_t devtools_command(int32_t argc, char **argv, char *out, size_t cap)
{
    if (argc < 2 || strcmp(argv[0], "snap") != 0)
        return 0;

    if (!strcmp(argv[1], "info")) {
        arena_init();
        snprintf(out, cap, "ok arena at %p: %u KB used, %u allocs, %u frees, free head %p",
                 (void *)AM2_ARENA_BASE, ARENA_HEAD->used / 1024,
                 ARENA_HEAD->allocs, ARENA_HEAD->frees, (void *)ARENA_HEAD->free_list);
        return 1;
    }
    if ((!strcmp(argv[1], "save") || !strcmp(argv[1], "load")) && argc >= 3) {
        struct timespec until;

        pthread_mutex_lock(&am2_req_lock);
        am2_req_kind = strcmp(argv[1], "save") == 0 ? 1 : 2;
        snprintf(am2_req_path, sizeof am2_req_path, "%s", argv[2]);
        am2_req_finished = 0;
        am2_req_reply[0] = 0;
        clock_gettime(CLOCK_REALTIME, &until);
        until.tv_sec += 10;
        while (!am2_req_finished) {
            if (pthread_cond_timedwait(&am2_req_done, &am2_req_lock, &until) == ETIMEDOUT)
                break;
        }
        if (am2_req_finished)
            snprintf(out, cap, "%s", am2_req_reply);
        else
            snprintf(out, cap, "err the game thread did not pump in 10 s (is it in a modal wait?)");
        am2_req_kind = 0;
        pthread_mutex_unlock(&am2_req_lock);
        return 1;
    }
    snprintf(out, cap, "err usage: snap save FILE | snap load FILE | snap info (FILE is absolute; the game chdirs)");
    return 1;
}

extern "C" void devtools_init(void)
{
    arena_init();
    am2_host_frame_hook = devtools_frame;
    hooklog("devtools: arena at %p, savestates on `snap save|load FILE`; F5 saves the game, F9 reloads it",
            (void *)AM2_ARENA_BASE);
}

#else
/* The release binary: nothing here. */
typedef int am2_devtools_absent;
#endif /* AM2_DEVTOOLS */
