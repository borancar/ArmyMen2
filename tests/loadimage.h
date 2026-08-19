/* Map the game image into the test process, without running the game.
 *
 * The selftest was restricted to functions that read no global data, on the
 * reasoning that a global "needs that global mapped, and mapping it means
 * starting the game". The second half of that is not true. The image is a file;
 * its sections can be copied to their virtual addresses by anyone, and a
 * reconstruction that only READS constant tables in .rdata/.data then works
 * exactly as it does in the real process.
 *
 * That matters for the script interpreter, which is built around tables in the
 * image -- ScriptLookupToken's 185 keywords at 0x00487C90 are the whole of what
 * it does. Testing it against the shipped mission scripts needs those bytes and
 * nothing else.
 *
 * It does NOT land at 0x00400000, and that is not a shortcut. Wine's loader
 * maps locale.nls across 0x00380000..0x00443000, plus c_1252, c_437 and
 * sortdefault through to 0x0084A000, before any user code runs -- so a process
 * that is not itself the game cannot have that range, and no link-time base
 * fixes it. Instead the image goes wherever VirtualAlloc puts it and
 * am2_image_slide (src/game/image.h) carries the difference; reconstructions
 * write their image addresses through AM2_IMAGE() and work either way.
 *
 * Nothing is executed here. The relocations are stripped and the imports are
 * unresolved, so the copied .text is inert data. Only reads of constant tables
 * are legitimate; a reconstruction that CALLS into the image, or reads a global
 * the game writes at runtime, is not testable this way and the in-process
 * check (AM2_SELFCHECK=1) remains the tool for those.
 */
#ifndef AM2_LOADIMAGE_H
#define AM2_LOADIMAGE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#include "../src/game/image.h"

#define AM2_IMAGE_BASE 0x00400000u

/* Returns 0 on success. The path is relative to the repository root, which is
 * where `make selftest` runs the test from. */
static int am2_load_image(const char *path)
{
    FILE *fh = fopen(path, "rb");
    if (!fh) {
        fprintf(stderr, "loadimage: cannot open %s\n", path);
        return 1;
    }

    fseek(fh, 0, SEEK_END);
    long len = ftell(fh);
    fseek(fh, 0, SEEK_SET);
    uint8_t *raw = (uint8_t *)malloc((size_t)len);
    if (!raw || fread(raw, 1, (size_t)len, fh) != (size_t)len) {
        fprintf(stderr, "loadimage: short read on %s\n", path);
        fclose(fh);
        free(raw);
        return 1;
    }
    fclose(fh);

    /* Use the real headers rather than restating them -- same rule as the
     * Win32 boundary. */
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)raw;
    IMAGE_NT_HEADERS32 *nt = (IMAGE_NT_HEADERS32 *)(raw + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
        nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.ImageBase != AM2_IMAGE_BASE) {
        fprintf(stderr, "loadimage: %s is not the expected image\n", path);
        free(raw);
        return 1;
    }

    uint32_t size = nt->OptionalHeader.SizeOfImage;
    void *at = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT,
                            PAGE_READWRITE);
    if (!at) {
        fprintf(stderr, "loadimage: cannot reserve %u bytes\n", size);
        free(raw);
        return 1;
    }
    am2_image_slide = (int32_t)((uintptr_t)at - AM2_IMAGE_BASE);

    IMAGE_SECTION_HEADER *sec = IMAGE_FIRST_SECTION(nt);
    for (uint32_t i = 0; i < nt->FileHeader.NumberOfSections; i++) {
        uint8_t *dst = (uint8_t *)at + sec[i].VirtualAddress;
        if (sec[i].SizeOfRawData)
            memcpy(dst, raw + sec[i].PointerToRawData, sec[i].SizeOfRawData);
    }

    free(raw);
    return 0;
}

#endif /* AM2_LOADIMAGE_H */
