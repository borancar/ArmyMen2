/* misc.cpp -- see misc.h. */
#include <stdint.h>

#include "misc.h"
#include "../inject/orig.h"

/* The original inlines `repne scasb` for length; a plain loop is the same
 * function and the vectors confirm it. */
static uint32_t StrLen(const char *s)
{
    uint32_t n = 0;

    while (s[n])
        n++;
    return n;
}

#include "../inject/patch.h"

uint32_t __cdecl Field53C(const void *p)
{
    return *(const uint32_t *)((const uint8_t *)p + 0x53C);
}

int32_t __cdecl AddByteSat(uint32_t base, int32_t add)
{
    int32_t sum = (int32_t)(base & 0xFFu) + add;

    if (sum > 0xFF)
        sum |= 0xFF;          /* `or al, 0xFF`, not a clamp -- see misc.h */
    return sum;
}

int32_t __cdecl CompareDword(const void *a, const void *b)
{
    return *(const int32_t *)a - *(const int32_t *)b;
}

void __cdecl CopyByteIfSet(uint32_t unused, uint8_t *dst, const void *src)
{
    const uint8_t *s = (const uint8_t *)src;

    (void)unused;
    if (!*(const uint32_t *)(s + 0x10))
        return;
    *dst = s[0x18];
}

int32_t __cdecl ScaleBy32Blocks(int32_t v)
{
    int32_t blocks = (v + (v < 0 ? 31 : 0)) >> 5;

    return (blocks + 1) * 1000;
}

void __cdecl TitleCaseName(char *text)
{
    int32_t i = 0;

    /* strlen is recomputed each time round, as the original does. */
    while (i < (int32_t)StrLen(text)) {
        if (text[i] == '_')
            text[i] = ' ';

        if (i > 0) {
            char prev = text[i - 1];

            if (prev == ' ' || prev == '.') {
                if (text[i] >= 'a' && text[i] <= 'z')
                    text[i] = (char)(text[i] - 0x20);
            }
        } else if (text[i] >= 'a' && text[i] <= 'z') {
            text[i] = (char)(text[i] - 0x20);
        }
        i++;
    }
}

void __cdecl ResetPairMask(uint32_t *a, uint32_t *b)
{
    *a = 0xFFFFFFFFu;
    *b = 0xFFFFFFFFu;
    *a &= 0xFFDBFFFDu;
}

int32_t __cdecl IsKind7(const void *p)
{
    return (*(const int32_t *)p == 7) ? 1 : 0;
}

int32_t __cdecl IsBlank(uint8_t c)
{
    return (c == ' ' || c == '\t' || c == '\r') ? 1 : 0;
}

int32_t __cdecl IsScriptDelim(uint8_t c)
{
    return (c == ')' || c == '(' || c == ',' || c == '<' || c == '=' ||
            c == '>' || c == '{' || c == '}' || c == '&' || c == '+') ? 1 : 0;
}

uint32_t __cdecl SwapColourBytes(uint32_t colour, uint32_t unused)
{
    (void)unused;
    return ((colour >> 16) & 0xFFu)
         | (((colour >> 8) & 0xFFu) << 8)
         | ((colour & 0xFFu) << 16);
}

void __stdcall NullStub4(uint32_t arg) { (void)arg; }
void __cdecl   NullStub(void)          { }
int32_t __cdecl ReturnZero(void)       { return 0; }
int32_t __cdecl ReturnOne(void)        { return 1; }

int misc_install(void)
{
    patch_replace(ADDR_FIELD_53C, (const void *)Field53C, "Field53C", 1);
    patch_replace(ADDR_ADD_BYTE_SAT, (const void *)AddByteSat, "AddByteSat", 2);
    patch_replace(ADDR_COMPARE_DWORD, (const void *)CompareDword, "CompareDword", 2);
    patch_replace(ADDR_COPY_BYTE_IF_SET, (const void *)CopyByteIfSet,
                  "CopyByteIfSet", 3);
    patch_replace(ADDR_SCALE_32_BLOCKS, (const void *)ScaleBy32Blocks,
                  "ScaleBy32Blocks", 1);
    patch_replace(ADDR_TITLE_CASE, (const void *)TitleCaseName, "TitleCaseName", 1);
    patch_replace(ADDR_RESET_PAIR_MASK, (const void *)ResetPairMask,
                  "ResetPairMask", 2);
    patch_replace(ADDR_IS_KIND_7, (const void *)IsKind7, "IsKind7", 1);
    patch_replace(ADDR_IS_BLANK, (const void *)IsBlank, "IsBlank", 1);
    patch_replace(ADDR_IS_SCRIPT_DELIM, (const void *)IsScriptDelim,
                  "IsScriptDelim", 1);
    patch_replace(ADDR_SWAP_COLOUR_BYTES, (const void *)SwapColourBytes,
                  "SwapColourBytes", 2);
    patch_replace(ADDR_NULL_STUB_4, (const void *)NullStub4, "NullStub4", 1);
    patch_replace(ADDR_NULL_STUB, (const void *)NullStub, "NullStub", 0);
    patch_replace(ADDR_RETURN_ZERO, (const void *)ReturnZero, "ReturnZero", 0);
    patch_replace(ADDR_RETURN_ONE, (const void *)ReturnOne, "ReturnOne", 0);
    return 0;
}
