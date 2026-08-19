/* misc.cpp -- small pure helpers that belong to no cluster yet.
 *
 * Each is reconstructed and verified against the original; what they are FOR
 * is in most cases not established, so they are named for what they compute.
 * When the surrounding translation unit is taken whole they should move to it.
 */
#ifndef AM2_MISC_H
#define AM2_MISC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
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

/* 0x0043EE80. Space, tab or carriage return. NOT newline -- a reconstruction
 * that reached for isspace() would accept '\n' and this does not. */
int32_t __cdecl IsBlank(uint8_t c);

/* 0x0043EEA0. One of ) ( , < = > { } & +.
 *
 * Confirmed rather than guessed: the parser's keyword table gives tokens 1 to
 * 13 as ( ) , < <= = > >= <> { } & + , and this set is exactly the first
 * character of each. It is the lexer asking "does a delimiter start here".
 * See docs/scripttokens.md. */
int32_t __cdecl IsScriptDelim(uint8_t c);

/* 0x0041AE90. Reverses the low three bytes and zeroes the fourth: 0x00BBGGRR
 * becomes 0x00RRGGBB. Sits beside the palette expander, so it is the channel
 * order swap between the file's colours and the display's. The second
 * parameter is read by nothing. */
uint32_t __cdecl SwapColourBytes(uint32_t colour, uint32_t unused);

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

int misc_install(void);

#ifdef __cplusplus
}
#endif

#endif /* AM2_MISC_H */
