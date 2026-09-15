/* printf.cpp -- _output, the printf engine, from 0x00468AD8, and the two
 * callers the game has: sprintf at 0x00464CE2 and vsprintf at 0x00465A45.
 *
 * _output is a state machine driven by the CRT's 89-byte table at
 * ADDR_CRT_PRINTF_TABLE: the low nibble of table[ch - ' '] classifies a
 * character, the high nibble of table[class * 8 + state] is the next
 * state, and the state selects one of eight arms. Everything else -- which
 * flags a conversion honours, how a width pads, where "(null)" comes from,
 * that %p is %X at precision 8, that an unknown conversion prints itself --
 * is read off the arms and reproduced in the same order.
 *
 * Output goes through write_char, which is the FILE's own buffer with
 * _flsbuf behind it; sprintf hands it a FILE whose buffer is the caller's
 * and whose count is INT_MAX, so _flsbuf is never reached from there. The
 * float conversions call into fltcvt.cpp, which is the CRT's own decimal
 * conversion; the 64-bit division helpers _aulldiv and _aullrem are what
 * the compiler emits, and a 64-bit divide in C is the same arithmetic.
 */
#include "crt.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#ifdef AM2_STANDALONE
/* The "(null)" printf substitutes for a null %s: the pointer slot at
 * 0x0048CC88 (crt_slid_pointer dereferences it). A one-entry char* table so
 * the blob "(null)" drops. (The wide sibling WNULLSTRING stays -- a UTF-16
 * slot does not fit the char* placement tooling.) */
extern "C" const char *const am2_crt_nullstring[1] = { "(null)" };
/* crt_output's format state machine at 0x0046FE70: table[ch-' '] classifies a
 * character in its low nibble, table[class*8 + state] gives the next state in
 * its high nibble. Pure const data, placed at its VA. */
extern "C" const uint8_t am2_crt_printf_table[92] = {
    0x06, 0x00, 0x00, 0x06, 0x00, 0x01, 0x00, 0x00, 0x10, 0x00, 0x03, 0x06, 0x00, 0x06, 0x02, 0x10,
    0x04, 0x45, 0x45, 0x45, 0x05, 0x05, 0x05, 0x05, 0x05, 0x35, 0x30, 0x00, 0x50, 0x00, 0x00, 0x00,
    0x00, 0x20, 0x28, 0x38, 0x50, 0x58, 0x07, 0x08, 0x00, 0x37, 0x30, 0x30, 0x57, 0x50, 0x07, 0x00,
    0x00, 0x20, 0x20, 0x08, 0x00, 0x00, 0x00, 0x00, 0x08, 0x60, 0x68, 0x60, 0x60, 0x60, 0x60, 0x00,
    0x00, 0x70, 0x70, 0x78, 0x78, 0x78, 0x78, 0x08, 0x07, 0x08, 0x00, 0x00, 0x07, 0x00, 0x08, 0x08,
    0x08, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x07, 0x08, 0x00, 0x00, 0x00,
};
#endif

/* Three pointer VARIABLES in the image, each holding an image address: the
 * slide applies to the variable and again to what it holds. */
static const void *crt_slid_pointer(uintptr_t var)
{
    return (const void *)(uintptr_t)AM2_IMAGE(*(const uint32_t *)(uintptr_t)AM2_IMAGE(var));
}
#define crt_pctype_table() ((const uint16_t *)crt_slid_pointer(ADDR_CRT_PCTYPE))

#define FL_SIGN        0x0001
#define FL_SIGNSP      0x0002
#define FL_LEFT        0x0004
#define FL_LEADZERO    0x0008
#define FL_LONG        0x0010
#define FL_SHORT       0x0020
#define FL_SIGNED      0x0040
#define FL_ALTERNATE   0x0080
#define FL_NEGATIVE    0x0100
#define FL_FORCEOCTAL  0x0200
#define FL_WIDECHAR    0x0800
#define FL_I64         0x8000

#define CRT_BUFFERSIZE 0x200

/* 0x00469219: one character into the stream, counting it, or -1 into the
 * count when _flsbuf refuses. */
static void crt_write_char(int32_t ch, CRT_FILE *f, int32_t *count)
{
    int32_t rc;

    if (--f->cnt >= 0) {
        *f->ptr++ = (char)ch;
        rc = (uint8_t)ch;
    } else {
        rc = crt_flsbuf(ch, f);
    }
    if (rc == -1)
        *count = -1;
    else
        ++*count;
}

/* 0x0046924E. */
static void crt_write_multi_char(int32_t ch, int32_t num, CRT_FILE *f, int32_t *count)
{
    while (num-- > 0) {
        crt_write_char(ch, f, count);
        if (*count == -1)
            break;
    }
}

/* 0x0046927F. */
static void crt_write_string(const char *s, int32_t len, CRT_FILE *f, int32_t *count)
{
    while (len-- > 0) {
        crt_write_char((int8_t)*s++, f, count);
        if (*count == -1)
            break;
    }
}

/* 0x0046B4BB: a wide character to its multibyte form, as the CRT's wctomb
 * over the single-byte page answers -- one byte below 0x100, -1 beyond. */
static int32_t crt_wctomb_out(char *out, uint32_t wc)
{
    if (wc < 0x100) {
        out[0] = (char)wc;
        return 1;
    }
    return -1;
}

int32_t __cdecl crt_output(CRT_FILE *stream, const char *format, va_list argptr)
{
    const uint8_t *table = (const uint8_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_PRINTF_TABLE);
    const char *nullstring = (const char *)crt_slid_pointer(ADDR_CRT_NULLSTRING);
    const uint16_t *wnullstring = (const uint16_t *)crt_slid_pointer(ADDR_CRT_WNULLSTRING);
    const uint16_t *pctype = crt_pctype_table();
    int32_t   state = 0, charsout = 0;
    int32_t   flags = 0, fldwidth = 0, precision = -1;
    int32_t   radix = 0, hexadd = 0, prefixlen = 0, textlen = 0;
    int32_t   no_output = 0, bufferiswide = 0, capexp = 0;
    char      prefix[2];
    const char *text = NULL;
    uint64_t  number = 0;
    char      buffer[CRT_BUFFERSIZE];
    int32_t   ch;

    prefix[0] = prefix[1] = 0;
    ch = (int8_t)*format++;
    while (ch != 0 && charsout >= 0) {
        int32_t chclass = (ch < ' ' || ch > 'x') ? 0 : (table[ch - ' '] & 0xF);
        state = table[chclass * 8 + state] >> 4;
        if (state > 7)
            goto next;

        switch (state) {
        case 0: {   /* ST_NORMAL: the character itself, a lead byte with its trail */
            bufferiswide = 0;
            if (pctype[(uint8_t)ch] & 0x8000) {
                crt_write_char(ch, stream, &charsout);
                ch = (int8_t)*format++;
            }
            crt_write_char(ch, stream, &charsout);
            break;
        }
        case 1:     /* ST_PERCENT */
            precision = -1;
            capexp = 0;
            no_output = 0;
            fldwidth = 0;
            prefixlen = 0;
            flags = 0;
            bufferiswide = 0;
            break;
        case 2:     /* ST_FLAG */
            switch (ch) {
            case ' ': flags |= FL_SIGNSP; break;
            case '#': flags |= FL_ALTERNATE; break;
            case '+': flags |= FL_SIGN; break;
            case '-': flags |= FL_LEFT; break;
            case '0': flags |= FL_LEADZERO; break;
            }
            break;
        case 3:     /* ST_WIDTH */
            if (ch == '*') {
                fldwidth = va_arg(argptr, int32_t);
                if (fldwidth < 0) {
                    flags |= FL_LEFT;
                    fldwidth = -fldwidth;
                }
            } else {
                fldwidth = fldwidth * 10 + (ch - '0');
            }
            break;
        case 4:     /* ST_DOT */
            precision = 0;
            break;
        case 5:     /* ST_PRECIS */
            if (ch == '*') {
                precision = va_arg(argptr, int32_t);
                if (precision < 0)
                    precision = -1;
            } else {
                precision = precision * 10 + (ch - '0');
            }
            break;
        case 6:     /* ST_SIZE */
            switch (ch) {
            case 'l': flags |= FL_LONG; break;
            case 'h': flags |= FL_SHORT; break;
            case 'w': flags |= FL_WIDECHAR; break;
            case 'I':
                if (format[0] == '6' && format[1] == '4') {
                    format += 2;
                    flags |= FL_I64;
                }
                break;
            }
            break;
        case 7:     /* ST_TYPE */
            switch (ch) {
            case 'C':
                if (!(flags & (FL_SHORT | FL_LONG | FL_WIDECHAR)))
                    flags |= FL_WIDECHAR;
                /* fall through */
            case 'c':
                bufferiswide = 0;
                if (flags & (FL_LONG | FL_WIDECHAR)) {
                    uint32_t wc = (uint16_t)va_arg(argptr, int32_t);
                    textlen = crt_wctomb_out(buffer, wc);
                    if (textlen < 0)
                        no_output = 1;
                } else {
                    buffer[0] = (char)va_arg(argptr, int32_t);
                    textlen = 1;
                }
                text = buffer;
                break;
            case 'S':
                if (!(flags & (FL_SHORT | FL_LONG | FL_WIDECHAR)))
                    flags |= FL_WIDECHAR;
                /* fall through */
            case 's': {
                int32_t i = precision == -1 ? 0x7FFFFFFF : precision;
                const void *p = va_arg(argptr, const void *);
                if (flags & (FL_LONG | FL_WIDECHAR)) {
                    const uint16_t *w = p ? (const uint16_t *)p : wnullstring;
                    const uint16_t *q = w;
                    bufferiswide = 1;
                    while (i-- && *q)
                        q++;
                    textlen = (int32_t)(q - w);
                    text = (const char *)w;
                } else {
                    const char *s = p ? (const char *)p : nullstring;
                    const char *q = s;
                    bufferiswide = 0;
                    while (i-- && *q)
                        q++;
                    textlen = (int32_t)(q - s);
                    text = s;
                }
                break;
            }
            case 'E':
            case 'G':
                capexp = 1;
                ch += 'a' - 'A';
                /* fall through */
            case 'e':
            case 'f':
            case 'g': {
                double value;
                flags |= FL_SIGNED;
                text = buffer;
                if (precision < 0)
                    precision = 6;
                else if (precision == 0 && ch == 'g')
                    precision = 1;
                value = va_arg(argptr, double);
                crt_cfltcvt(&value, buffer, ch, precision, capexp);
                if ((flags & FL_ALTERNATE) && precision == 0)
                    crt_forcdecpt(buffer);
                if (ch == 'g' && !(flags & FL_ALTERNATE))
                    crt_cropzeros(buffer);
                if (buffer[0] == '-') {
                    flags |= FL_NEGATIVE;
                    text = buffer + 1;
                }
                textlen = crt_strlen(text);
                break;
            }
            case 'n': {
                void *p = va_arg(argptr, void *);
                if (flags & FL_SHORT)
                    *(int16_t *)p = (int16_t)charsout;
                else
                    *(int32_t *)p = charsout;
                no_output = 1;
                break;
            }
            case 'p':
                precision = 8;
                /* fall through */
            case 'X':
                hexadd = 7;
                goto hex;
            case 'x':
                hexadd = 0x27;
            hex:
                radix = 16;
                if (flags & FL_ALTERNATE) {
                    prefix[0] = '0';
                    prefix[1] = (char)(hexadd + 0x51);
                    prefixlen = 2;
                }
                goto number;
            case 'o':
                radix = 8;
                if (flags & FL_ALTERNATE)
                    flags |= FL_FORCEOCTAL;
                goto number;
            case 'd':
            case 'i':
                flags |= FL_SIGNED;
                /* fall through */
            case 'u':
                radix = 10;
            number:
                if (flags & FL_I64) {
                    number = va_arg(argptr, uint64_t);
                } else if (flags & FL_SHORT) {
                    if (flags & FL_SIGNED)
                        number = (uint64_t)(int64_t)(int16_t)va_arg(argptr, int32_t);
                    else
                        number = (uint16_t)va_arg(argptr, int32_t);
                } else {
                    if (flags & FL_SIGNED)
                        number = (uint64_t)(int64_t)va_arg(argptr, int32_t);
                    else
                        number = (uint32_t)va_arg(argptr, int32_t);
                }
                if ((flags & FL_SIGNED) && (int64_t)number < 0) {
                    number = (uint64_t)(-(int64_t)number);
                    flags |= FL_NEGATIVE;
                }
                if (!(flags & FL_I64))
                    number &= 0xFFFFFFFFu;
                if (precision < 0)
                    precision = 1;
                else
                    flags &= ~FL_LEADZERO;
                if (number == 0)
                    prefixlen = 0;
                {
                    char *p = &buffer[CRT_BUFFERSIZE - 1];
                    while (precision-- > 0 || number != 0) {
                        int32_t digit = (int32_t)(number % (uint32_t)radix) + '0';
                        number /= (uint32_t)radix;
                        if (digit > '9')
                            digit += hexadd;
                        *p-- = (char)digit;
                    }
                    textlen = (int32_t)(&buffer[CRT_BUFFERSIZE - 1] - p);
                    p++;
                    if ((flags & FL_FORCEOCTAL) && (textlen == 0 || *p != '0')) {
                        *--p = '0';
                        textlen++;
                    }
                    text = p;
                }
                break;
            default:
                /* The state table sent an unlisted type here; the original's
                 * switch has no default arm, so nothing is emitted. */
                break;
            }

            if (!no_output) {
                int32_t padding;
                if (flags & FL_SIGNED) {
                    if (flags & FL_NEGATIVE) {
                        prefix[0] = '-';
                        prefixlen = 1;
                    } else if (flags & FL_SIGN) {
                        prefix[0] = '+';
                        prefixlen = 1;
                    } else if (flags & FL_SIGNSP) {
                        prefix[0] = ' ';
                        prefixlen = 1;
                    }
                }
                padding = fldwidth - textlen - prefixlen;
                if (!(flags & (FL_LEFT | FL_LEADZERO)))
                    crt_write_multi_char(' ', padding, stream, &charsout);
                crt_write_string(prefix, prefixlen, stream, &charsout);
                if ((flags & FL_LEADZERO) && !(flags & FL_LEFT))
                    crt_write_multi_char('0', padding, stream, &charsout);
                if (bufferiswide && textlen > 0) {
                    const uint16_t *w = (const uint16_t *)text;
                    int32_t n = textlen;
                    while (n-- > 0) {
                        char    mb[8];
                        int32_t len = crt_wctomb_out(mb, *w++);
                        if (len <= 0) {
                            charsout = -1;
                            break;
                        }
                        crt_write_string(mb, len, stream, &charsout);
                    }
                } else {
                    crt_write_string(text, textlen, stream, &charsout);
                }
                if (flags & FL_LEFT)
                    crt_write_multi_char(' ', padding, stream, &charsout);
            }
            break;
        }
    next:
        ch = (int8_t)*format++;
    }
    return charsout;
}

/* 0x00465A45. A FILE over the caller's buffer: writable string, INT_MAX to
 * spare, then the terminator the same way _output writes. */
int32_t __cdecl crt_vsprintf(char *buf, const char *format, va_list args)
{
    CRT_FILE f;
    int32_t  n;

    f.ptr = buf;
    f.base = buf;
    f.cnt = 0x7FFFFFFF;
    f.flag = 0x42;
    n = crt_output(&f, format, args);
    if (--f.cnt < 0)
        crt_flsbuf(0, &f);
    else
        *f.ptr++ = 0;
    return n;
}

/* 0x00464CE2. */
int32_t __cdecl crt_sprintf(char *buf, const char *format, ...)
{
    va_list args;
    int32_t n;

    va_start(args, format);
    n = crt_vsprintf(buf, format, args);
    va_end(args);
    return n;
}
