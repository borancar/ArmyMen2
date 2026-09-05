/* io.h -- the MSVC find-file family, which src/inject/standalone.h and
 * src/standalone/runtime.cpp reach by these names. The struct is MSVC 6's
 * _finddata_t with a 32-bit time_t: 0x118 bytes, which is the frame slot
 * the reconstruction's callers reserve for it (AM2_FINDDATA_BYTES). */
#ifndef AM2_PLATFORM_IO_H
#define AM2_PLATFORM_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH   0x20

struct _finddata_t {
    uint32_t attrib;
    int32_t  time_create;
    int32_t  time_access;
    int32_t  time_write;
    uint32_t size;
    char     name[260];
};

intptr_t _findfirst(const char *spec, struct _finddata_t *out);
int32_t  _findnext(intptr_t handle, struct _finddata_t *out);
int32_t  _findclose(intptr_t handle);
int32_t  _chmod(const char *path, int32_t mode);
int32_t  _access(const char *path, int32_t mode);

#ifdef __cplusplus
}
#endif

#endif /* AM2_PLATFORM_IO_H */
