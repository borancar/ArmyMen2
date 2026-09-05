/* stdio.cpp -- the FILE layer: _flsbuf so far.
 *
 * The rest -- fopen, fclose, fread, fwrite, fgets, fseek, ftell, fflush,
 * _filbuf, the _iob table and the lowio handles beneath them -- follows;
 * this is the arm sprintf's FILE can reach, so printf.cpp links.
 */
#include "crt.h"

#define CRT_IOREAD  0x01
#define CRT_IOWRT   0x02
#define CRT_IORW    0x80
#define CRT_IOSTRG  0x40
#define CRT_IOERR   0x20

/* 0x00466AB3. The first test in the original: a stream that is not open
 * for writing, or is a string stream, has nowhere to flush to -- it marks
 * the error and answers -1. The file arm, which allocates the buffer and
 * calls _write, comes with the rest of stdio. */
int32_t __cdecl crt_flsbuf(int32_t ch, CRT_FILE *f)
{
    (void)ch;
    if (!(f->flag & (CRT_IOWRT | CRT_IORW)) || (f->flag & CRT_IOSTRG)) {
        f->flag |= CRT_IOERR;
        return -1;
    }
    /* Not reached until stdio.cpp is complete: no FILE this build opens
     * reaches here, since the game's file streams are still the host's. */
    f->flag |= CRT_IOERR;
    return -1;
}
