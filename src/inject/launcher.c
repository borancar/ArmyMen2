/* launcher.exe -- starts ArmyMen2.exe with am2hook.dll already inside it.
 *
 * The game is created suspended, our DLL is loaded into it via a remote
 * LoadLibraryA call, and only then is the main thread resumed. That ordering
 * matters: the harness gets to install its patches before a single instruction
 * of the game's entry point has run.
 *
 * Nothing on disk is touched -- no proxy DLLs, no modified import table, no
 * patched executable. Stop using the launcher and the install is stock again.
 *
 * Usage: wine launcher.exe [path\to\ArmyMen2.exe] [game args...]
 */

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DEFAULT_EXE "C:\\GOG Games\\Army Men II\\ArmyMen2.exe"
#define HOOK_DLL    "am2hook.dll"

/* Directory containing this launcher, with trailing backslash. */
static int launcher_dir(char *out, size_t cap)
{
    char *slash;

    if (!GetModuleFileNameA(NULL, out, (DWORD)cap))
        return 1;
    slash = strrchr(out, '\\');
    if (!slash)
        return 1;
    slash[1] = '\0';
    return 0;
}

static int inject(HANDLE proc, const char *dll)
{
    size_t  len = strlen(dll) + 1;
    void   *remote;
    HANDLE  thread;
    DWORD   exit_code = 0;
    FARPROC load_library;

    /* kernel32 is mapped at the same address in every process of a given
     * bitness, so our LoadLibraryA is also the child's LoadLibraryA. */
    load_library = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");
    if (!load_library) {
        fprintf(stderr, "launcher: no LoadLibraryA\n");
        return 1;
    }

    remote = VirtualAllocEx(proc, NULL, len, MEM_COMMIT | MEM_RESERVE,
                            PAGE_READWRITE);
    if (!remote) {
        fprintf(stderr, "launcher: VirtualAllocEx failed (%lu)\n", GetLastError());
        return 1;
    }
    if (!WriteProcessMemory(proc, remote, dll, len, NULL)) {
        fprintf(stderr, "launcher: WriteProcessMemory failed (%lu)\n", GetLastError());
        return 1;
    }

    thread = CreateRemoteThread(proc, NULL, 0,
                                (LPTHREAD_START_ROUTINE)load_library, remote,
                                0, NULL);
    if (!thread) {
        fprintf(stderr, "launcher: CreateRemoteThread failed (%lu)\n", GetLastError());
        return 1;
    }

    WaitForSingleObject(thread, 15000);
    GetExitCodeThread(thread, &exit_code);
    CloseHandle(thread);
    VirtualFreeEx(proc, remote, 0, MEM_RELEASE);

    if (exit_code == 0) {
        fprintf(stderr, "launcher: remote LoadLibraryA returned NULL "
                        "(is %s next to the launcher?)\n", dll);
        return 1;
    }
    printf("launcher: %s loaded at 0x%08lx\n", dll, exit_code);
    return 0;
}

int main(int argc, char **argv)
{
    char exe[MAX_PATH];
    char dll[MAX_PATH];
    char dir[MAX_PATH];
    char cmdline[1024];
    char *slash;
    int   i;
    STARTUPINFOA        si;
    PROCESS_INFORMATION pi;

    snprintf(exe, sizeof exe, "%s", argc > 1 ? argv[1] : DEFAULT_EXE);

    if (launcher_dir(dll, sizeof dll)) {
        fprintf(stderr, "launcher: cannot locate own directory\n");
        return 1;
    }
    strncat(dll, HOOK_DLL, sizeof dll - strlen(dll) - 1);

    /* Run with the game's own folder as cwd -- it loads its data by relative path. */
    snprintf(dir, sizeof dir, "%s", exe);
    slash = strrchr(dir, '\\');
    if (slash)
        *slash = '\0';

    snprintf(cmdline, sizeof cmdline, "\"%s\"", exe);
    for (i = 2; i < argc; i++) {
        strncat(cmdline, " ", sizeof cmdline - strlen(cmdline) - 1);
        strncat(cmdline, argv[i], sizeof cmdline - strlen(cmdline) - 1);
    }

    printf("launcher: exe %s\n", exe);
    printf("launcher: dll %s\n", dll);

    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    ZeroMemory(&pi, sizeof pi);

    if (!CreateProcessA(exe, cmdline, NULL, NULL, FALSE, CREATE_SUSPENDED,
                        NULL, slash ? dir : NULL, &si, &pi)) {
        fprintf(stderr, "launcher: CreateProcess failed (%lu)\n", GetLastError());
        return 1;
    }
    printf("launcher: created pid %lu, suspended\n", pi.dwProcessId);

    if (inject(pi.hProcess, dll)) {
        TerminateProcess(pi.hProcess, 1);
        return 1;
    }

    ResumeThread(pi.hThread);
    printf("launcher: resumed\n");

    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
