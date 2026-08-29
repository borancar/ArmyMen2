/* misc.cpp -- small pure helpers that belong to no cluster yet.
 *
 * Each is reconstructed and verified against the original; what they are FOR
 * is in most cases not established, so they are named for what they compute.
 * When the surrounding translation unit is taken whole they should move to it.
 */
#ifndef AM2_MISC_H
#define AM2_MISC_H

#include <stdint.h>

#include "dist.h"   /* AM2_Point, which ObjMaskBitAt takes */

/* IsBlank and IsScriptDelim moved to script.cpp: 0x0043EE80 and 0x0043EEA0
 * are inside script.cpp's own address band, and they are the tokeniser's two
 * character predicates. */

#ifdef __cplusplus
extern "C" {
/* 0x0042E770. Short movie name -> filename: the name, "sml" when the small
 * set is in use, then ".smk". Here rather than in a movie module because
 * nothing else of that subsystem is reconstructed yet. */
void __cdecl MovieBuildName(char *dst, const char *name);

/* 0x00461870, two callers. Walk one seq context and step every live record;
 * the walk is index-chained through SEQ_OFF_NEXT rather than sequential. */
void __cdecl SeqRun(void *ctx);

/* 0x00461310, and both kinds 2 and 3 reach it: a two-frame life. */
int32_t __cdecl SeqStepKind2(int32_t at, void *rec, void *ctx);

/* 0x00461660. The third adder -- kind 6, whose sprite is a VARIANT of eight
 * frames rather than a frame, and whose depth key carries the terrain. */
void __cdecl SeqAddKind6(const int32_t *at, int32_t variant);

/* 0x00462000 and 0x00462080. Add one SEQ record at a map point -- kind 5 and
 * kind 7. Were ADDR_BY_REF_ACTION_A and _B; see misc.cpp. */
void __cdecl SeqAddKind5(const int32_t *at, int32_t owner, int32_t life);
void __cdecl SeqAddKind7(const int32_t *at, int32_t owner, int32_t b,
                         int32_t c, int32_t life);

#endif

/* 0x0045AFA0. The dword at +0x53C. */
uint32_t __cdecl Field53C(const void *p);

/* 0x0045F440. Adds `add` to the low byte of `base`.
 *
 * The saturation is `or al, 0xFF`, NOT a clamp to 255: a sum of 0x150 comes
 * back as 0x1FF, because only the low byte is forced. Reconstructing this as
 * `min(sum, 255)` would be tidier and would not be this function. The compare
 * is signed, so a negative `add` returns the negative sum untouched. */
int32_t __cdecl AddByteSat(uint32_t base, int32_t add);

/* 0x0043E150. Subtracts the dwords two pointers address, which is the shape of
 * a qsort comparator -- and the difference, not the sign, so it overflows for
 * far-apart values exactly as the original does. */
int32_t __cdecl CompareDword(const void *a, const void *b);

/* 0x00408560. Copies the byte at +0x18 of `src` to `*dst`, but only when the
 * dword at +0x10 is non-zero. The first parameter is read by nothing. */
void __cdecl CopyByteIfSet(uint32_t unused, uint8_t *dst, const void *src);

/* 0x0042E4F0. (v / 32 + 1) * 1000, with the division rounding toward zero --
 * the original adds 31 before shifting when v is negative. The *1000 is three
 * lea *5 chains and a shl 3, which is 125 * 8. */
int32_t __cdecl ScaleBy32Blocks(int32_t v);

/* 0x0042E510. Tidies a name in place: '_' becomes ' ', and a lowercase letter
 * is capitalised when it starts the string or follows a space or a period.
 *
 * It recomputes strlen on every iteration, so it is quadratic in the length.
 * That is the original's and is kept -- the strings are short and matching the
 * instruction count is worth more here than the better loop. */
void __cdecl TitleCaseName(char *text);

/* 0x0042F120. Stores -1 through both pointers, then clears three bits of the
 * first: *a ends as 0xFFDBFFFD, *b as 0xFFFFFFFF. Written as two stores and an
 * and, which is what the original does rather than one constant each. */
void __cdecl ResetPairMask(uint32_t *a, uint32_t *b);

/* 0x00435640. The dword at +0 equals 7. No null check, unlike the ObjIsTypeN
 * family, so it is not one of them however much it looks like one. */
int32_t __cdecl IsKind7(const void *p);



/* 0x0041AE90. Reverses the low three bytes and zeroes the fourth: 0x00BBGGRR
 * becomes 0x00RRGGBB. Sits beside the palette expander, so it is the channel
 * order swap between the file's colours and the display's. The second
 * parameter is read by nothing. */
uint32_t __cdecl SwapColourBytes(uint32_t colour, uint32_t unused);

/* 0x0041B760. Two colours' distance: the sum of the squares of the three
 * per-channel differences, each an absolute value taken with the
 * `cdq`/`xor`/`sub` idiom rather than a branch.
 *
 * It reads THREE bytes from each side, so a palette entry's fourth byte takes
 * no part -- which is why the caller can pass a plain colour and a palette
 * slot and have both mean the same thing.
 *
 * It lives HERE rather than beside its only caller in win32/palette.cpp
 * because it is pure, and the flat half is what `make selftest` can link. */
int32_t __cdecl ColourDistance(const void *a, const void *b);

/* Four that do nothing but return. Reconstructed because they are functions in
 * the original and something calls them; there is nothing else to say. */
void     __stdcall NullStub4(uint32_t arg);   /* 0x004170E0, `ret 4` */
void     __cdecl   NullStub(void);            /* 0x0042E170, bare `ret` */
int32_t  __cdecl   ReturnZero(void);          /* 0x0042E980 */
int32_t  __cdecl   ReturnOne(void);           /* 0x004354F0 */

/* 0x004231A0. Copies `count` blocks of `total / count` bytes from src to dst,
 * taking the SOURCE blocks in reverse order while the destination advances --
 * a block reverse, which over rows of a bitmap is a vertical flip. Returns 1,
 * including when count <= 0 and it copies nothing. */
/* 0x00423200. Open `path`, read one DIB chunk into the 0x428-byte header
 * `hdr`, and answer the pixels flipped top-down in a buffer the caller owns.
 * `*size` is the LISTED SIZE from the header -- the same field the allocation
 * and the "size of 0" complaint use -- and is zeroed on every failure after
 * the file opens. The block count is a different field, 0x08, and only
 * ReverseBlocks sees it.
 *
 * The flip is ReverseBlocks over the rows, which is what a bottom-up DIB
 * needs. The chunk reader hands back a buffer this frees.
 *
 * It leaks that buffer if the destination allocation fails -- the only exit
 * that does not free it. Reproduced; it is one `malloc` failure away from
 * mattering and nothing here is in a position to decide otherwise.
 *
 * Its only caller loads `%02d_%03d_%02d_*.msk` out of a `masks` directory, so
 * the masks are DIBs. The function itself knows nothing about masks and is not
 * named for them. */

int32_t __cdecl ReverseBlocks(void *dst, const void *src, int32_t total,
                              int32_t count);

/* 0x004374F0. The script language's comparison: op 0 is equal, 1 is less, 2 is
 * greater, anything else false. That is the reduced form of tokens 6, 4 and 7
 * -- the scripts write `if test3 testvar tankilled = 3 then` and
 * `... reztimes < 15 ...`, which is where these three come from.
 *
 * Note the operand order: every arm compares the THIRD argument against the
 * first, so op 1 answers `b < a`. */
int32_t __cdecl ScriptCompare(int32_t a, int32_t op, int32_t b);

/* The debug logger at 0x0045CAA0 is NOT here. src/inject/gamelog.c already
 * un-stubs it behind AM2_GAMELOG=1, and does it defensively -- some call sites
 * pass binary data where a format string should be, because the body was a
 * no-op for the whole of the game's shipping life and nothing ever validated
 * them. A plain vsnprintf there is a reliable segfault. */

/* 0x00435EB0. Orders two records on a two-dword key: the first field, then the
 * second. It returns the DIFFERENCE of whichever field decided, not the sign,
 * so it overflows for far-apart values -- a qsort comparator written the way
 * everyone writes them and wrong for the same reason. */
int32_t __cdecl ComparePair(const void *a, const void *b);

/* 0x00406920. Maps 1..30 to a small constant through a jump table, everything
 * else to zero. The mapping is not a formula -- the original really is a
 * 30-entry index table into eight arms -- so it is a table here too. */
int32_t __cdecl MapCode(int32_t code);

/* 0x00435A80. ComparePair with a third field: keys at +0, +4 and +8, first
 * difference wins, difference returned rather than its sign. */
int32_t __cdecl CompareTriple(const void *a, const void *b);

/* 0x00433570. Whether two type codes go together. Only 8, 9 and 10 can say
 * yes, and each has its own rule -- 8 pairs with 29, 9 with anything but 9,
 * 10 with 29 or 8. Everything else is no.
 *
 * The dispatch is a 22-entry index table over five arms, so the shape is a
 * switch the compiler flattened rather than a formula. Written out as the
 * three cases it is. */
int32_t __cdecl TypesCompatible(int32_t a, int32_t b);

/* 0x0043D450 and 0x0045C5E0. The same function twice, over two different
 * records: an eight-way selector writes a mode field and a character taken
 * from src+0x40, shifted by 0x20 up, down, or not at all.
 *
 * 0x20 on a byte is the ASCII case bit, so what these produce is a letter in
 * one case or the other -- the shape of building an animation or file name
 * from a base letter and a facing. The mode field takes 2 or 1 in the first
 * and 4 or 1 in the second, and the pair of them differ only in the offsets
 * they write, so the two records are laid out differently and hold the same
 * thing.
 *
 * Selector 1 jumps into selector 3's body past its first instruction, so those
 * two arms are the same and are written that way. Above 7 nothing is written
 * at all -- the out record is left as it was, not zeroed. */
void __cdecl SetFacing14(int32_t facing, const void *src, void *out);
void __cdecl SetFacing08(int32_t facing, const void *src, void *out);

/* 0x0044BBF0 and 0x00433500. Two membership tests over the same kind of code
 * the ObjIsTypeN predicates compare against: 10 through 17 inclusive, and
 * exactly 14 or 22. Signed comparisons, so a negative code is outside both. */
int32_t __cdecl IsKind10To17(int32_t kind);
int32_t __cdecl IsKind14Or22(int32_t kind);

/* 0x0040D7E0. Classifies an object into 0, 1 or 2 from a code two hops away:
 * the pointer at obj+0x74, then a SIGNED 16-bit field at +0x4C of that.
 *
 * The code is biased by 7 and looked up in a 64-entry table, so the useful
 * range is 7..70 and anything outside gives 0. The table is not a formula --
 * the ones and twos are scattered -- so it is a table here too, read out of
 * 0x0040D820 and the three arms at 0x0040D814.
 *
 * What the classes MEAN is unknown, and so is the record at +0x74. Two things
 * are worth saying about the shape: the field is signed, so a negative code is
 * possible and lands outside the range; and 7..70 is the kind of span an
 * object-type enumeration has, which is the sort of thing data/<map>/object.aai
 * lists in its Type column. */
int32_t __cdecl ClassifyByCode74(const void *obj);

/* 0x0045EE20 and 0x00433520. Two membership tests over a kind code, each a
 * jump table with only "yes" and "no" arms, so what they really are is a
 * scattered set written as a switch.
 *
 * The accepted sets are in the .cpp. They are NOT script tokens -- running the
 * accepted values through the table in docs/scripttokens.md gives a mix of
 * delimiters and entity names that means nothing, so this is a different code
 * space, most likely the object type numbering that object.aai lists. Named
 * for what they do until that is settled. */
int32_t __cdecl KindInSetA(int32_t kind);   /* range 2..42 */
int32_t __cdecl KindInSetB(int32_t kind);   /* range 1..29 */

/* 0x0041CF20. Whether pixel (x, y) of a run-length encoded mask is solid.
 *
 * The record is a word width at +0, a word height at +2, a table of word row
 * offsets at +4 -- relative to the START of the record, not to the table --
 * and then rows encoded as repeating [skip][run][run bytes]: skip transparent
 * pixels, then `run` solid ones whose data follows inline.
 *
 * Everything is compared UNSIGNED and 16-bit. The running total is kept in a
 * 32-bit register but only `cx` is ever compared, so it wraps at 65536 rather
 * than saturating, and the reconstruction accumulates in uint16_t to match.
 * A negative x arrives as a large unsigned word and fails the bounds test,
 * which is why there is no signed check anywhere. */
int32_t __cdecl MaskPixelSolid(uint32_t x, uint32_t y, const void *mask);

/* 0x0041CEC0. The same decoder over the same format with ONE difference: the
 * row table holds dword offsets rather than word ones, so it is read at
 * `m + 4 + y * 4` and the rows can start beyond 64K. Everything else -- the
 * two bounds words, the [skip][run] walk, the 16-bit accumulator and both
 * exits -- is instruction for instruction the same, which is why the two
 * share a body here.
 *
 * It sits immediately below MaskPixelSolid in the image, 0x0041CEC0 against
 * 0x0041CF20, so the pair was almost certainly one source function with the
 * row width as a compile-time choice.
 *
 * THE VECTORS CANNOT CHECK THE ONE THING THAT DISTINGUISHES IT. A row offset
 * has to land inside the emulator's 0x8000-byte scratch to be followed at all,
 * so its high word is always zero, so reading the entry as a word gives the
 * same answer as reading it as a dword. Mutating the dword read back to a word
 * passes every vector. The word/dword difference is verified by reading the
 * two disassemblies and by nothing else; everything below the row lookup is
 * covered. */
int32_t __cdecl MaskPixelSolid32(uint32_t x, uint32_t y, const void *mask);

/* 0x004232C0. One bit of the 1bpp bitmap the mask above is encoded FROM --
 * both call sites are in the encoder at 0x00423300, which walks a row
 * counting runs and caps each at 255, producing exactly the [skip][run]
 * stream MaskPixelSolid decodes.
 *
 * Rows are addressed `(height - y - 1) * stride`, which is bottom-up: row 0
 * of the image is the LAST row in memory. That is the DIB convention, and it
 * is the only reason this function needs the height at all.
 *
 * The x modulo is signed -- the original uses the MSVC `and 0x80000007` /
 * `dec` / `or` / `inc` sequence, which is C's `%` and keeps the sign of the
 * dividend -- so a negative x shifts by more than 7 and the result is the
 * original's, whatever that is worth. Nothing establishes that a negative x
 * ever arrives; it is reproduced rather than defended.
 *
 * Returns the masked BIT, not a boolean: 0 or 1 << (7 - x % 8). The one
 * caller tests it against zero. */
int32_t __cdecl BitmapBitSet(const void *base, int32_t x, int32_t y,
                             int32_t height, int32_t stride);

/* 0x00435390. Whether a map point falls on a solid pixel of an object's own
 * 1bpp mask. The mask hangs off the object at +0x78 and both it and the object
 * may be null, each answering 0.
 *
 * The point is put into mask space by the object's position:
 *
 *   x = mask.originX - obj[0x30] + at->x
 *   y = mask.originY - obj[0x34] + at->y
 *
 * with the origin a pair of int16 at +0 and +2 of the mask, the extent another
 * pair at +4 and +6, and the pixels behind a pointer at +0xC. Rows are
 * `((width + 31) >> 3) & ~3` bytes -- 1bpp rounded up to a whole dword, the
 * usual Windows row alignment -- which is the third row stride this file now
 * carries and the only one that is dword-aligned.
 *
 * Accessed by byte offset rather than through a struct on purpose: the record
 * ends in a pointer, and a struct would be 4 bytes wider on this target and
 * quietly move every field after it.
 *
 * The original selects the bit from a table of {0x80, 0x40 ... 1} it builds on
 * the stack every call rather than shifting. Reproduced as the shift, which is
 * the same eight values; the table is a compiler's choice about how to spell
 * `0x80 >> (x & 7)` and nothing observes the difference.
 *
 * Returns the masked BIT, not a boolean, like BitmapBitSet. */
int32_t __cdecl ObjMaskBitAt(const void *obj, const AM2_Point *at);


/* 0x00402700. XOR of the record's own dwords, its length taken from inside it:
 * the dword at +4 is a BYTE count, shifted right by two for a dword count. The
 * sum starts at +0, so the length field is folded into its own checksum.
 *
 * The count is shifted unsigned and then tested signed, which after a shift by
 * two can only be zero -- so `jle` there is `== 0` however it reads. */
uint32_t __cdecl XorChecksum(const void *record);

/* 0x004010B0. Follows the pointer at +4 and returns the dword at +0x14 of it,
 * or 0 when that pointer is null. */
uint32_t __cdecl ChainField14(const void *p);

/* 0x00429F20. Pushes a node onto the front of a doubly linked list: prev at
 * +4, next at +8, and `head` is the address of the head pointer. */
void __cdecl ListPushFront(void *node, void **head);

/* 0x0041DAD0. The other half of the same list: take `node` out and leave it
 * with both links cleared. Same node layout as ListPushFront -- prev at +4,
 * next at +8 -- and the same (node, head) signature, and six callers around
 * 0x00429E4B..0x0042A0E4, which is where ListPushFront lives.
 *
 * `head` is written ONLY when the node is the first one, because that is the
 * only case where the head has to move. An unlink from the middle never
 * touches it, so the pointer is allowed to be wrong there and nothing would
 * notice -- reproduced, not defended. */
void __cdecl ListUnlink(void *node, void **head);

/* 0x0041BB60. Copy `count` bytes from src to dst, each one through `table` if
 * there is one. With table NULL it is a plain forward copy instead, and the
 * two paths are not symmetric:
 *
 *   table != NULL   `count <= 0` returns, and the copy is a byte loop.
 *   table == NULL   `dst == src` returns, and there is NO count check at all
 *                   -- the count goes straight into `rep movsd` / `rep movsb`
 *                   as unsigned, so a negative one would copy 4GB. Both call
 *                   sites mask it to a byte first (`and edi, 0xFF`), which is
 *                   why that has never mattered.
 *
 * Reproduced as a dword loop then a byte loop rather than as memcpy, because
 * `rep movs` copies FORWARD and memcpy is undefined on overlap; dst and src
 * are only known to be different, not disjoint. */
/* 0x0042E310. Read a sprite's {set, index, frame} back out of a filename in
 * the "%02d_%03d_%02d_*.bmp" convention. All three outputs are cleared before
 * either test. */
int32_t __cdecl ParseSpriteName(const char *name, int32_t *set,
                                int32_t *index, int32_t *frame);

void __cdecl RemapBytes(void *dst, const void *src, const void *table,
                        int32_t count);

/* 0x00434E90. Writes `value` into the dword at +0x2C of every element of an
 * array the record describes -- base at +8, count at +4, elements 0x60 apart.
 * Returns the number written, which is 0 when the count is not positive.
 *
 * Base and count are re-read from the record on EVERY iteration rather than
 * held in registers, and that is reproduced: if the array ever overlapped the
 * record, the loop would see its own writes, and hoisting them would quietly
 * change that. */
int32_t __cdecl SetFieldInAll(void *record, void *value);

/* 0x0040A490. Whether the byte at +0x51 is at least the word at +6 of the
 * record pointed to by +0x44; no record means no.
 *
 * The byte is zero-extended to 16 bits and the comparison is SIGNED, so a
 * threshold above 0x7FFF reads as negative and everything passes. */
int32_t __cdecl Field51MeetsMin(const void *p);

/* 0x0040D860. Whether the dword at +0x538 is between 10 and 17 inclusive --
 * the same range IsKind10To17 tests, read from the object instead of taken as
 * an argument. Signed comparisons, so a negative value is outside. */
int32_t __cdecl ObjKind538In10To17(const void *obj);

/* 0x0040D880. Which value of +0x538 the object should take, given the one
 * asked for. Returns the CURRENT value when the change is refused, so a caller
 * that stores the result unconditionally gets "no change" for free.
 *
 * Three things decide it, and the two tables at 0x0040D8F0 and 0x0040D8FC put
 * every code into exactly one of three arms:
 *
 *   0x00, 0x20..0x24            skip the readiness test below
 *   0x05, 0x08..0x0F            use the requested code as the override
 *   everything else, and every  neither
 *   code above 0x24
 *
 * The readiness test is also skipped when the current value is 1. It reads a
 * sub-record at +0x74: a pointer at its +0x44 whose first int16 is a count,
 * against an unsigned byte at its +0x51. If that byte has not reached
 * count - 1, the change is refused. That is the shape of "this sequence has
 * not finished yet", with the byte a position and the word a length -- and it
 * explains the first arm, which is the set of codes allowed to interrupt.
 *
 * Then the override: the dword at +0x53C, when it is non-zero, replaces the
 * answer -- with the requested code itself if the code was in the second arm,
 * and with +0x53C's own value otherwise.
 *
 * Named for the field, not for what the field means. +0x538 is the same one
 * ObjKind538In10To17 tests and +0x53C the one Field53C reads, and neither of
 * those established what either holds. */
int32_t __cdecl ObjNextKind538(const void *obj, int32_t want);

/* 0x0041ADE0. Builds a 3-3-2 palette into the caller's buffer: the top three
 * bits of the index become the first channel, the next three the second, and
 * the low two the third, each expanded to 0..255 by `v * 255 / n` with n 7, 7
 * and 3. Index 0 comes out black and index 0xFF white, which is the check that
 * the channel order is not reversed.
 *
 * Two tables, 256 entries each and 2048 bytes in total:
 *
 *   +0x000   four bytes per entry, [c0][c1][c2][0]
 *   +0x400   one dword per entry, (c2 << 16) | (c1 << 8) | c0
 *
 * Those are PALETTEENTRY and COLORREF laid out by hand -- 0xE0 gives
 * (255, 0, 0) and packs to 0x0000FF, which is COLORREF's 0x00BBGGRR with red
 * full. The names are in this comment and not in the code: the function names
 * no Win32 type, takes a buffer the caller supplies and touches nothing else,
 * so it belongs on the flat side of the split by the same test blit.cpp
 * passes. Its one caller hands the result straight to SetGamePalette.
 *
 * The original divides by 7 and by 3 with the usual MSVC reciprocal-multiply
 * sequences (0x92492493 shift 2, and 0x55555556). Written as division, which
 * truncates the same way for the eight and four non-negative values that can
 * reach it -- checked against all of them, not argued from the general case. */
void __cdecl BuildRgb332Palette(void *out);

/* 0x00439CC0. Collapse runs of constant difference in a 16-bit array, in
 * place, and rewrite the count. The first element is always kept; after that,
 * every maximal run whose consecutive differences are all equal is replaced by
 * its LAST element.
 *
 *   10 20 30 40 50 60 100 101   count 8
 *   10 60 100 101               count 4
 *
 * A count of 1 or less is not walked at all: the count is set to 1 and the
 * array left alone, even when the count was 0 or negative.
 *
 * Values are read as UNSIGNED 16-bit and subtracted in 32 bits, so a
 * difference is never negative and never wraps -- 0x0000 after 0xFFFF is a
 * difference of -65535, not 1. That is the original's arithmetic, from its
 * `xor edx,edx; mov dx,[...]` pairs, and it is reproduced rather than
 * corrected.
 *
 * The running comparison reads the last KEPT value back out of the array
 * rather than holding it in a register, which is why the reconstruction does
 * too: it is what makes the collapse cumulative rather than pairwise.
 *
 * Named for the criterion. What the array holds is not established -- one
 * caller, no strings anywhere near it, and the pad.cpp..script.cpp band is
 * as far as the evidence goes. */
void __cdecl CollapseEqualDeltas(uint16_t *values, int32_t *count);

/* 0x00423EE0. Rewrite every pixel of an RLE image through a 256-byte lookup
 * table, in place. The format is the one MaskPixelSolid decodes -- width and
 * height as int16 at +0 and +2, a row-offset table at +4, and rows of
 * [skip][run][run bytes] walked until the accumulated width reaches the image
 * width -- and `wide` selects the row table's entry size, dword when it is
 * exactly 1 and word for every other value.
 *
 * That is the same pair MaskPixelSolid and MaskPixelSolid32 are, which is what
 * this function settles: the two decoders are not near-duplicates that happened
 * to be compiled twice, they are the two halves of a format that carries its
 * offset width as a parameter.
 *
 * Both open questions here are answered by the one call site, which is now
 * reconstructed -- SpriteLoadFromDataFile in win32/sprite.cpp. `wide` is the
 * sprite's FORMAT, 1, 2 or 3, straight out of the file: the same selector
 * sprite.h describes, so the parameter and the union arm are one decision made
 * twice. This comment said "the one caller passes 3", which is true of only
 * one of that call site's two arms.
 *
 * And the second argument, read as "either vestigial or part of a shared
 * signature", is the RLE block's byte LENGTH. The caller has just malloc'd and
 * fread that many bytes and passes the count on; this function walks the row
 * table instead and never looks at it. Still reproduced as an ignored
 * parameter, because dropping it would change the calling convention.
 *
 * The accumulator is 16-bit and only its low half is ever compared, so a row
 * whose runs overshoot wraps rather than saturating -- the same arithmetic
 * MaskPixelSolid's comment already describes. A run length of zero writes
 * nothing and still counts toward the width, which is how a row ends. */
void __cdecl RemapRleRuns(void *rle, void *unused, int32_t wide,
                          const void *table);

/* 0x0041EF20. Two-criterion match, the shape this binary uses wherever a list
 * is filtered. Each criterion is one of three things:
 *
 *   a wildcard    -- -1 for the first, ZERO for the second. They differ, and
 *                    they differ in more than value: -1 in the first returns
 *                    immediately, so it skips the SECOND criterion as well,
 *                    while 0 in the second only ends its own test. Reading
 *                    that jump target as the start of the second criterion
 *                    rather than as the success return is how this went in
 *                    wrong the first time
 *   a bitmask     -- when the value is negative, i.e. the sign bit is set, it
 *                    matches if (want & have) == want, so it is a subset test
 *                    rather than equality
 *   an exact id   -- otherwise
 *
 * Both must pass. `haveA`/`haveB` are the candidate's values and
 * `maskA`/`maskB` the sets it belongs to. */
int32_t __cdecl FilterMatches(int32_t wantA, int32_t wantB,
                              int32_t haveA, int32_t haveB,
                              int32_t maskA, int32_t maskB);

/* 0x00408520. Consumes a pending byte at +0x104 of `src`: does nothing if it
 * is zero, otherwise writes it into *dst when `cfg` permits, and clears it
 * either way.
 *
 * Two independent decisions come off `cfg`. The byte at +0x2C above 0x40 --
 * UNSIGNED, so 0x80 counts as above -- sets *(int32 *)(dst + 0x14) to 3. And
 * the pending byte only reaches *dst when the dword at +0x10 is zero.
 *
 * The pending byte is read TWICE, and it has to be: loading `cfg` clobbers the
 * register the first read used, so the original goes back for it. */
void __cdecl ConsumePendingByte(void *src, void *dst, const void *cfg);

/* 0x0045C870 and 0x0043D550. The same function over the two records SetFacing08
 * and SetFacing14 write -- same offset pairs, +8/+0xC and +0x14/+0x18 -- and it
 * produces what those two consume: a facing code from 0 to 7.
 *
 * A signed delta becomes a code, with a dead zone of +-16 either side of zero:
 *
 *   mode == 1        delta < 0 gives 7, otherwise 2
 *   mode != 1, flag  delta >= 16 gives 5, > -16 gives 4, otherwise 3
 *   mode != 1, !flag delta >= 16 gives 1, > -16 gives 0, otherwise 7
 *
 * The original computes the last two arms with setg and a borrow rather than
 * branches, and the third does `and al, 0xFB` on the LOW BYTE of a value that
 * is 0 or -1, so the -1 case comes back as 0xFFFFFFFB and the following add
 * wraps to 2. Written out as the values it produces, which is what a caller
 * sees. */
int32_t __cdecl FacingFromDelta08(const void *rec, int32_t delta);
int32_t __cdecl FacingFromDelta14(const void *rec, int32_t delta);

/* 0x00406A40. Maps a code in 0x18..0x28 to a small constant, everything else
 * to zero: 0x18 gives 8, 0x19 gives 2, 0x1A gives 1, 0x27 gives 4, 0x28 gives
 * 6. Seventeen entries over five arms, so a table there and a table here. */
int32_t __cdecl MapCode18To28(int32_t code);

/* 0x00449EF0. Reads a code through a pointer -- obj->[0xC0]->[0] -- and answers
 * 0 for exactly five of them: 0x18, 0x19, 0x1A, 0x27 and 0x28. Everything else
 * answers 1, including every code outside 0x18..0x28, which the range test
 * sends straight to the same arm.
 *
 * Those five are not an arbitrary set. MapCode18To28 is a different function
 * in a different part of the image with a table of its own, and the entries it
 * maps to NONZERO -- 8, 2, 1 at 0x18..0x1A and 4, 6 at 0x27..0x28 -- are the
 * same five, with 0x1B..0x26 mapping to zero. So this is exactly
 * `MapCode18To28(code) == 0`, over the whole domain including out of range.
 *
 * Reproduced with its own table rather than by calling that one. They are two
 * functions in the image and the correspondence is an observation about the
 * two tables, not a guarantee about either; writing one in terms of the other
 * would make a future divergence impossible to see. */
int32_t __cdecl ObjCodeUnmapped(const void *obj);

/* 0x00409650. Three conditions, all of which must hold: bit 2 of the byte at
 * +8 CLEAR, the dword at +0 equal to 1, and the dword at the start of the
 * record pointed to by +0x94 equal to 0x1F. */
int32_t __cdecl MeetsAllThree(const void *p);

/* 0x0040F250 and 0x0040F200, both thiscall on the comm object. The player
 * records start at AM2_PLAYER_ARMY and are AM2_PLAYER_STRIDE apart.
 *
 * The first answers which slot flies a colour. Army 4 is answered with 4
 * before the table is looked at, and a colour no slot holds comes back as slot
 * 0 -- the same answer as "slot 0 is that colour", so a caller cannot tell the
 * two apart. That is the original's shape, not a simplification.
 *
 * The second answers whether a slot still holds a real player, by testing the
 * DirectPlay id the find-by-id search matches on. See ADDR_COMM_SLOT_HAS_PLAYER
 * in orig.h for why it is not the "is AI" it was first taken for. */
int32_t __attribute__((thiscall)) CommSlotForArmy(void *comm, int32_t army);

/* 0x0040F2F0. The DirectPlay id of a comm slot; slot -1 answers 0, and there
 * is no upper bound. */
int32_t __attribute__((thiscall)) CommPlayerId(void *comm, int32_t slot);

/* 0x00402F00. A random value averaging `centre`: `100 - spread` draws of
 * rand() % (centre * 2), divided by that count. `spread` is an inverse
 * tightness, not a range -- more spread means fewer samples. */
int32_t __cdecl RandomAround(int32_t centre, int32_t spread);

/* 0x0040F8A0 and 0x0040F8E0. Does every player with a real AM2_PLAYER_ID have
 * AM2_PLAYER_AGREED, and AM2_PLAYER_READY, set? An empty table answers yes. */
int32_t __attribute__((thiscall)) CommAllPlayersAgreed(void *comm);
int32_t __attribute__((thiscall)) CommAllPlayersReady(void *comm);

/* 0x0040F1C0, thiscall. The same walk, answering the matching slot's
 * COMM_ARMY_OFF_WAS_HERE rather than its index. */
int32_t __attribute__((thiscall)) CommWasHereForArmy(void *comm, int32_t army);

/* 0x0040F960, thiscall. One army's score, out of the script name table. */
int32_t __attribute__((thiscall)) GetArmyScore(void *comm, int32_t slot);

/* 0x00421800. Decides whether a multiplayer mission was won and shows the end
 * screen. The argument it passes on is "LOST", not "won" -- both arms invert
 * a `sete` before the call. */
void __cdecl MissionNetworked(int32_t army, int32_t teamGame);
int32_t __attribute__((thiscall)) CommSlotHasPlayer(void *comm, int32_t slot);

/* 0x0040F280, thiscall. Gives a slot an army colour, SWAPPING with whoever
 * already held it, so no two players end up the same. -1 for a negative
 * colour; otherwise the slot it was given. Only the host rearranges. */
int32_t __attribute__((thiscall)) CommSetArmyColour(void *comm, int32_t slot,
                                                    int32_t colour);

/* 0x00412DD0, 21 callers and five bytes of body: one load of the menu row and
 * a `ret`. A getter over a global, which is why so much of the menu code goes
 * through it rather than reading the global. */
int32_t __cdecl GetMenuRow(void);

/* 0x0042A660 and 0x0042A670, both thiscall over the three-field {a, b, ptr}
 * record. The first zeroes all three and frees nothing, which is a
 * constructor; the second is one `jmp` to the teardown at 0x0042A680, which is
 * still original -- the same shape as FreeSpriteListAlias, and reconstructed
 * as the alias it is rather than left as a jump to somewhere else. */
/* It returns `this`, like every i386 MSVC constructor, and the caller at
 * 0x0040A628 stores the result. Declared `void` this is the RecordCtor defect
 * exactly -- see widget.h -- and it was found by the same sweep rather than by
 * anything going wrong, because nothing has reached that caller yet. */
void *__attribute__((thiscall)) InitPtrList(void *rec);
void __attribute__((thiscall)) ClearPtrListAlias(void *rec);

/* 0x0042A680, six callers -- what that alias jumps to, and the last of the
 * record's five. Zero the capacity and the count, free the items if there are
 * any, zero that too. The two fields are cleared BEFORE the free, which is
 * what the original does and what makes it safe to call twice. */
void __attribute__((thiscall)) ClearPtrList(void *rec);

/* 0x00427450 and 0x00427470, 33 and 21 callers -- the two keyboard predicates
 * everything in the game tests a key with. Both mask the scancode to 8 bits
 * themselves, so a caller never has to.
 *
 * IsKeyDown answers the top bit of the CURRENT buffer, and returns 0x80 rather
 * than 1 -- an `and eax, 0x80` with no normalisation, so callers must be
 * testing it against zero. KeyChanged xors the two buffers and shifts the top
 * bit down, so it answers 0 or 1.
 *
 * Which buffer is current alternates: PollKeyboard SWAPS the two pointers each
 * poll, which is why these read through ADDR_KEYS_NOW_PTR rather than a fixed
 * array. */
int32_t __cdecl IsKeyDown(int32_t dik);
int32_t __cdecl KeyChanged(int32_t dik);

/* 0x00427430 and 0x004274D0, the other two of the keyboard four.
 *
 * KeyPressed reads the edge-and-auto-repeat array at 0x00512BD0 rather than
 * either poll buffer, which is what most of the game actually tests.
 *
 * LatchKeyState copies the whole 256-byte current buffer over the previous
 * one, so every edge test that follows sees no change -- called where the game
 * wants the keystroke that got it here not to be seen again. Note it COPIES
 * where PollKeyboard SWAPS; the two are different operations on the same
 * pair. */
int32_t __cdecl KeyPressed(int32_t dik);
void __cdecl LatchKeyState(void);

/* 0x004274A0 and 0x004274F0, the last two of the keyboard family.
 *
 * ConsumeKey copies one key's current state over its previous one and clears
 * its entry in the pressed array, so the edge is not seen twice.
 *
 * ActionKeyDown is the one the game actually asks 26 times over: an ACTION
 * index, not a scancode. Two scancodes are bound to each -- a primary and an
 * alternate -- and either being down is enough. Where there is no alternate
 * the table holds 0, and that is tested too rather than skipped. */
void __cdecl ConsumeKey(int32_t dik);
int32_t __cdecl ActionKeyDown(int32_t action);

/* 0x00427530, 19 callers -- the same two keys, but "just pressed": a key
 * counts only if it is down AND its bit differs between the two poll buffers.
 * The alternate is tested the same way, so an action with no alternate tests
 * scancode 0 and gets nothing. */
int32_t __cdecl ActionKeyPressed(int32_t action);

/* 0x004275B0. The mirror of the above: true when either bound key for this
 * action has just come UP. `!down && changed`. */
int32_t __cdecl ActionKeyReleased(int32_t action);

/* 0x00424900. True when SPACE, F1, ESCAPE or RETURN has just come up -- and it
 * CONSUMES that key, so the answer cannot be asked for twice. */
int32_t __cdecl DismissKeyReleased(void);

/* 0x0040F190, 47 callers -- CommSlotForArmy's inverse. Slot 4 answers 4
 * without touching the object, the same convention army 4 has everywhere. */
int32_t __attribute__((thiscall)) CommArmyOfSlot(void *comm, int32_t slot);

/* 0x0042A6E0, 13 callers. Append to the {capacity, count, items} record
 * InitPtrList clears, growing it first when the count has caught the
 * capacity. The grow is still original. */
void __attribute__((thiscall)) PtrListPush(void *rec, void *item);

/* 0x0042A6B0 and 0x0042A710. The same record's two capacity moves, and they
 * are not symmetric: the grow adds twenty and reallocs, while the shrink
 * takes twenty off and FREES the array outright when that leaves nothing,
 * rather than reallocing to zero. Neither touches the count. */
void __attribute__((thiscall)) PtrListGrow(void *rec);
void __attribute__((thiscall)) PtrListShrink(void *rec);

/* 0x0042A750, 33 callers -- the same record's remove. Drop the entry at
 * `index`, shift what follows down with the CRT's memmove, and hand the
 * capacity back through 0x0042A710 once twenty entries are spare. An index at
 * or past the count does nothing.
 *
 * MSVC wrote the byte count as `((index << 30) - index + count) << 2`, which
 * is `(count - index) * 4` because the shifted term leaves the register
 * entirely. Written as the multiply it is. */
void __attribute__((thiscall)) ListRemoveAt(void *rec, int32_t index);

/* 0x00434C80, one caller. Free a pointer unless it is null, and nothing else.
 * The CRT's own free makes the same test, so this exists to save a call rather
 * than to be necessary. */
void __cdecl FreeIfNotNull(void *p);


/* 0x0043A5E0, three callers. One dword out of the unit-type table at
 * 0x004878B8 -- 12 records of 40 bytes whose names are IN them: bazookaman,
 * mortarman, grenadier, flamerman, tank, jeep, halftrack, truck, ptboat,
 * riflepill, bazookapill, mgpill.
 *
 * That identity is certain. What the field means is not: it runs 100 to 500,
 * this is its only reader, and the callers are the two mission-start screens,
 * which is what suggests a cost and is not proof of one. No bounds check. */
uint32_t __cdecl UnitTypeCost(int32_t type);

int misc_install(void);

#ifdef __cplusplus
}
#endif

/* Original: 0x0043B7C0. Hand every abandoned army to the AI. Network games
 * only, so verified by reading. */
void __cdecl AiTakeAbandoned(void);

/* Original: 0x00461930. Run the seq walker over both contexts. */
void __cdecl SeqRunBoth(void);

#endif /* AM2_MISC_H */
