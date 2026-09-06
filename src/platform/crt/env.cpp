/* env.cpp -- getenv, from the body at 0x0046CDFA.
 *
 * It walks _environ, the "NAME=value" strings startup copied out of
 * GetEnvironmentStrings, comparing names case-insensitively through the
 * locale. Until startup has run it answers NULL for everything -- which
 * in the native build is always, since nothing there runs the original's
 * startup; the hybrid's original does, and the table it builds is the
 * one this reads there.
 */
#include "crt.h"
#include "../../inject/orig.h"
#include "../../game/image.h"

#define crt_env_initialized (*(int32_t *)(uintptr_t)AM2_IMAGE(ADDR_CRT_ENV_INITIALIZED))
#define crt_environ         (*(char ***)(uintptr_t)AM2_IMAGE(ADDR_CRT_ENVIRON))
#define crt_wenviron        (*(void **)(uintptr_t)AM2_IMAGE(ADDR_CRT_WENVIRON))

char *__cdecl crt_getenv(const char *name)
{
    char  **env;
    int32_t len;

    if (!crt_env_initialized)
        return NULL;
    env = crt_environ;
    if (env == NULL) {
        /* A wide environment and no narrow one: convert. This image's
         * startup builds the narrow one, so the arm is never taken. */
        if (crt_wenviron == NULL)
            return NULL;
        if (crt_wtomb_environ() != 0)
            return NULL;
        env = crt_environ;
        if (env == NULL)
            return NULL;
    }
    if (name == NULL)
        return NULL;
    len = crt_strlen(name);
    for (; *env != NULL; env++) {
        if (crt_strlen(*env) > len && (*env)[len] == '='
            && crt_mbsnbicoll(*env, name, (uint32_t)len) == 0)
            return *env + len + 1;
    }
    return NULL;
}
